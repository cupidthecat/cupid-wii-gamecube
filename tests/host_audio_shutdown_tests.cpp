// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/output.hpp"
#include "test_support.hpp"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <latch>
#include <optional>
#include <poll.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
using namespace std::chrono_literals;

class Process {
public:
  explicit Process(pid_t value) : pid(value) {
    CHECK(pid > 0);
  }

  ~Process() {
    if (pid > 0) {
      kill(pid, SIGCONT);
      kill(pid, SIGTERM);
      if (!wait(1s)) {
        kill(pid, SIGKILL);
        int status = 0;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }
      }
    }
  }

  Process(const Process &) = delete;
  Process &operator=(const Process &) = delete;

  void signal(int value) const {
    CHECK(pid > 0 && kill(pid, value) == 0);
  }

  std::optional<int> wait(std::chrono::milliseconds bound) {
    const auto deadline = std::chrono::steady_clock::now() + bound;
    do {
      int status = 0;
      const auto result = waitpid(pid, &status, WNOHANG);
      if (result == pid) {
        pid = -1;
        return status;
      }
      if (result < 0 && errno != EINTR) {
        pid = -1;
        return -1;
      }
      std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return {};
  }

private:
  pid_t pid;
};

class Pipe {
public:
  Pipe() {
    CHECK(pipe(descriptors.data()) == 0);
  }

  ~Pipe() {
    close(descriptors[0]);
    close(descriptors[1]);
  }

  Pipe(const Pipe &) = delete;
  Pipe &operator=(const Pipe &) = delete;

  void send(char value) const {
    ssize_t result = 0;
    do {
      result = write(descriptors[1], &value, 1);
    } while (result == -1 && errno == EINTR);
    CHECK(result == 1);
  }

  char receive(int milliseconds) const {
    pollfd descriptor{descriptors[0], POLLIN, 0};
    int result = 0;
    do {
      result = poll(&descriptor, 1, milliseconds);
    } while (result == -1 && errno == EINTR);
    CHECK(result == 1 && (descriptor.revents & POLLIN));
    char value = 0;
    CHECK(read(descriptors[0], &value, 1) == 1);
    return value;
  }

private:
  std::array<int, 2> descriptors{};
};

void environment(const char *name, const std::string &value) {
  CHECK(setenv(name, value.c_str(), 1) == 0);
}

template <typename Action> [[noreturn]] void child(Action action) {
  try {
    action();
    std::cout.flush();
    std::cerr.flush();
    _exit(0);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n' << std::flush;
    _exit(1);
  }
}

bool single_threaded_within(std::chrono::milliseconds bound) {
  const auto deadline = std::chrono::steady_clock::now() + bound;
  while (std::distance(std::filesystem::directory_iterator("/proc/self/task"),
                       std::filesystem::directory_iterator{}) != 1) {
    if (std::chrono::steady_clock::now() >= deadline)
      return false;
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

void thread_cleanup_predicate() {
  std::latch started(1);
  std::jthread live([&](std::stop_token stop) {
    started.count_down();
    while (!stop.stop_requested())
      std::this_thread::sleep_for(1ms);
  });
  started.wait();
  const bool accepted_live_thread = single_threaded_within(10ms);
  live.request_stop();
  live.join();
  CHECK(!accepted_live_thread);
  CHECK(single_threaded_within(100ms));
}

void worker(const std::filesystem::path &socket, const Pipe &commands, const Pipe &events) {
  environment("SDL_AUDIODRIVER", "pulseaudio");
  environment("PULSE_SERVER", "unix:" + socket.string());
  {
    cupid::platform::OutputOptions options;
    options.audio = true;
    cupid::platform::Output output(options);
    std::array<cupid::s16, 32768> samples;
    samples.fill(16000);
    output.audio(samples, 48000);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (output.host_audio_stats().mixed_frames < 8192) {
      CHECK(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(10ms);
    }
    CHECK(output.host_audio_stats().nonzero_samples > 0);
    events.send('R');
    CHECK(commands.receive(5000) == 'S');
    const auto before = output.host_audio_stats().mixed_frames;
    std::this_thread::sleep_for(150ms);
    CHECK(output.host_audio_stats().mixed_frames == before);
    events.send('F');
    CHECK(commands.receive(5000) == 'C');
  }
  CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);
  // A successful pthread join can precede Linux removing the exited task from /proc.
  // Keep the thread-leak check bounded inside the parent's stopped-server close deadline.
  const bool threads_gone = single_threaded_within(100ms);
  if (!threads_gone)
    for (const auto &entry : std::filesystem::directory_iterator("/proc/self/task")) {
      std::ifstream status(entry.path() / "status");
      std::cerr << "Remaining task " << entry.path().filename() << '\n';
      std::string line;
      while (std::getline(status, line))
        std::cerr << line << '\n';
    }
  CHECK(threads_gone);
  events.send('D');
}

void stalled_server_close(const std::filesystem::path &server_binary) {
  const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory = std::filesystem::temp_directory_path() /
                         ("cupid-pulse-" + std::to_string(getpid()) + "-" + std::to_string(serial));
  CHECK(std::filesystem::create_directory(directory));
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
  // Retain the fixture's private files for diagnosis.
  const auto socket = directory / "native";
  const auto config = directory / "pulse.pa";
  const auto state = directory / "state";
  CHECK(std::filesystem::create_directory(state));
  {
    std::ofstream out(config);
    out << "load-module module-null-sink sink_name=owned_test rate=48000 channels=2\n"
        << "load-module module-native-protocol-unix socket=" << socket.string()
        << " auth-anonymous=1\nset-default-sink owned_test\n";
    CHECK(out.good());
  }
  std::cout << "Owned Pulse fixture: " << directory.string() << '\n' << std::flush;
  const auto server_pid = fork();
  CHECK(server_pid >= 0);
  if (server_pid == 0) {
    child([&] {
      environment("PULSE_RUNTIME_PATH", directory.string());
      environment("PULSE_STATE_PATH", state.string());
      environment("XDG_RUNTIME_DIR", directory.string());
      environment("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + (directory / "no-dbus").string());
      const auto file_option = "--file=" + config.string();
      const auto *modules = std::getenv("CUPID_TEST_PULSE_MODULE_PATH");
      const auto module_option =
          modules ? "--dl-search-path=" + std::string(modules) : std::string("--log-level=warning");
      execl(server_binary.c_str(), server_binary.c_str(), "-n", "--daemonize=no",
            "--use-pid-file=no", "--exit-idle-time=-1", "--disable-shm=yes", "--realtime=no",
            "--high-priority=no", file_option.c_str(), module_option.c_str(),
            static_cast<char *>(nullptr));
      throw std::runtime_error("Cannot execute owned Pulse server");
    });
  }
  Process server(server_pid);
  const auto start_deadline = std::chrono::steady_clock::now() + 3s;
  while (!std::filesystem::exists(socket)) {
    CHECK(std::chrono::steady_clock::now() < start_deadline);
    CHECK(!server.wait(0ms));
    std::this_thread::sleep_for(10ms);
  }
  Pipe commands, events;
  const auto worker_pid = fork();
  CHECK(worker_pid >= 0);
  if (worker_pid == 0) {
    child([&] { worker(socket, commands, events); });
  }
  Process output(worker_pid);
  CHECK(events.receive(5000) == 'R');
  server.signal(SIGSTOP);
  std::this_thread::sleep_for(250ms);
  commands.send('S');
  CHECK(events.receive(2000) == 'F');
  commands.send('C');
  auto status = output.wait(1s);
  const bool closed_while_stopped = status.has_value();
  // Recover our server even on the red path, so an unchanged SDK is safely testable.
  server.signal(SIGCONT);
  if (!status)
    status = output.wait(2s);
  CHECK(status.has_value());
  CHECK(WIFEXITED(*status) && WEXITSTATUS(*status) == 0);
  CHECK(events.receive(2000) == 'D');
  CHECK(closed_while_stopped);
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "A private Pulse server executable is required\n";
    return 1;
  }
  return run_tests([&] {
    thread_cleanup_predicate();
    stalled_server_close(argv[1]);
  });
}
