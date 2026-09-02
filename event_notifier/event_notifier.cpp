#include <iostream>
#include <string>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <atomic>
#include <termios.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "common/common.h"

namespace {
constexpr const char* kSocketPath = "/tmp/event_sock.sock";
std::atomic<bool> g_should_stop{false};

void SignalHandler(int signum) {
    syslog(LOG_INFO, "Received signal: %d", signum);
    g_should_stop.store(true);
}
} // namespace

class EventNotifier {
public:
    EventNotifier(): sock_fd_(-1) {
        openlog("EventNotifier", LOG_PID | LOG_NDELAY, LOG_USER);
        syslog(LOG_INFO, "EventNotifier created");

        // Setup terminal mode
        if(tcgetattr(STDIN_FILENO, &old_termios_) == -1) {
            syslog(LOG_ERR, "Failed to get terminal attributes");
        }
        struct termios new_termios = old_termios_;
        new_termios.c_lflag &= ~(ICANON | ECHO);
        new_termios.c_cc[VMIN] = 0;   // read() returns even with 0 bytes available
        new_termios.c_cc[VTIME] = 1;  // ...after waiting up to 100ms
        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == -1) {
            syslog(LOG_ERR, "Failed to set terminal to non-canonical mode");
        }
    }

    ~EventNotifier() {
        if (sock_fd_ != -1) {
            close(sock_fd_);
        }
        //restaurete canonical terminal mode
        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios_);
        syslog(LOG_INFO, "Exiting EventNotifier");
    }
    
    Status Connect() {
        sock_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if(sock_fd_ == -1) {
            syslog(LOG_ERR, "Error on opening socket: %s", kSocketPath);
            return Status::Failure;
        }

        struct sockaddr_un sock_addr{};
        std::memset(&sock_addr, 0, sizeof(sock_addr));
        sock_addr.sun_family = AF_UNIX;
        std::strncpy(sock_addr.sun_path, kSocketPath, sizeof(sock_addr.sun_path) - 1);

        if(connect(sock_fd_, reinterpret_cast<struct sockaddr*>(&sock_addr), sizeof(sock_addr)) == -1) {
            syslog(LOG_ERR, "Error on connecting to socket: %s", kSocketPath);
            return Status::Failure;
        }

        return Status::OK;
    }

    void CatchEvents() {
        while(!g_should_stop.load()) {
            char c;
            ssize_t n = read(STDIN_FILENO, &c, 1);
            if(n > 0 && c == 'e') {
                Notify("Trigger");
                syslog(LOG_INFO, "Event sent");
            }
        }
    }

private:

    Status Notify(const char * event) {
        ssize_t bytes_sent = send(sock_fd_, event, std::strlen(event), 0);
        if (bytes_sent == -1) {
            syslog(LOG_ERR, "Error sending event notification");
            return Status::Failure;
        }
        return Status::OK;
    }

    int32_t sock_fd_;
    struct termios old_termios_;
};

int main(int argc, char* argv[]) {
    std::signal(SIGINT, SignalHandler);

    EventNotifier notifier;

    if(notifier.Connect() != Status::OK) {
        std::cerr << "Failed to connect to Video Clipper socket" << std::endl;
        return 1;
    }

    std::cout << "Connected. Press 'e' to send an event, Ctrl+C to quit." << std::endl;
    notifier.CatchEvents();

    return 0;
}