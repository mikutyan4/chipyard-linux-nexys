/**
 * Interactive UART TSI - version with real-time user input support
 *
 * Implementation overview:
 * 1. Create a pipe and redirect stdin to the pipe's read end
 * 2. Start an input thread that reads user input from the original stdin
 * 3. The input thread writes data to the pipe's write end
 * 4. fesvr's sys_read reads from the pipe's read end (non-blocking)
 * 5. The main loop continues processing UART without blocking on input
 *
 * Usage:
 *   ./uart_tsi_interactive +tty=/dev/ttyUSB0 program.riscv
 */

#include "testchip_uart_tsi_interactive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include <signal.h>
#include <pthread.h>
#include <atomic>
#include <time.h>
#include <deque>

#define PRINTF(...) printf("UART-TSI-Interactive: " __VA_ARGS__)

// Output detection related
static int g_real_stdout_fd = -1;        // The real stdout
static int g_stdout_pipe_read = -1;      // stdout pipe read end
static int g_stdout_pipe_write = -1;     // stdout pipe write end
static std::deque<char> g_output_buffer; // Recent output character buffer
static const size_t OUTPUT_BUFFER_SIZE = 100; // Keep the most recent 100 characters
static std::atomic<bool> g_detected_jumping(false); // Detected "Jumping"

// Global variables for the input thread
static int g_original_stdin = -1;      // Saved original stdin
static int g_pipe_write_fd = -1;       // Pipe write end
static std::atomic<bool> g_input_thread_running(false);
static pthread_t g_input_thread;
static struct termios g_orig_termios;
static bool g_terminal_modified = false;
static bool g_local_echo = true;       // Local echo (can be disabled with +noecho)
static bool g_attach_mode = false;     // Attach mode (skip loading)
static uint64_t g_override_tohost = 0;   // Override tohost address
static uint64_t g_override_fromhost = 0; // Override fromhost address

// Global pointer for signal handling
static testchip_uart_tsi_interactive_t* g_tsi = nullptr;

// Restore terminal settings
static void restore_terminal_settings() {
  if (g_terminal_modified) {
    tcsetattr(g_original_stdin, TCSANOW, &g_orig_termios);
    g_terminal_modified = false;
  }
}

// Set up stdout interception
static bool setup_stdout_interception() {
  // Save the real stdout
  g_real_stdout_fd = dup(STDOUT_FILENO);
  if (g_real_stdout_fd < 0) {
    fprintf(stderr, "Failed to dup stdout\n");
    return false;
  }
  
  // Create pipe
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    fprintf(stderr, "Failed to create stdout pipe\n");
    close(g_real_stdout_fd);
    return false;
  }
  
  g_stdout_pipe_read = pipefd[0];
  g_stdout_pipe_write = pipefd[1];
  
  // Set the pipe read end to non-blocking
  int flags = fcntl(g_stdout_pipe_read, F_GETFL, 0);
  fcntl(g_stdout_pipe_read, F_SETFL, flags | O_NONBLOCK);
  
  // Redirect stdout to the pipe write end
  if (dup2(g_stdout_pipe_write, STDOUT_FILENO) < 0) {
    fprintf(stderr, "Failed to redirect stdout\n");
    close(g_real_stdout_fd);
    close(g_stdout_pipe_read);
    close(g_stdout_pipe_write);
    return false;
  }
  
  return true;
}

// Restore stdout
static void restore_stdout() {
  if (g_real_stdout_fd >= 0) {
    dup2(g_real_stdout_fd, STDOUT_FILENO);
    close(g_real_stdout_fd);
    g_real_stdout_fd = -1;
  }
  if (g_stdout_pipe_read >= 0) {
    close(g_stdout_pipe_read);
    g_stdout_pipe_read = -1;
  }
  if (g_stdout_pipe_write >= 0) {
    close(g_stdout_pipe_write);
    g_stdout_pipe_write = -1;
  }
}

// Process intercepted stdout output, detect "reconnecting" (after "Jumping")
// This ensures sdboot finishes all output before switching addresses
static int g_switch_delay_counter = 0;  // Delay counter
static const int SWITCH_DELAY_ITERATIONS = 1000;  // Number of iterations to wait after detection

static void process_intercepted_stdout() {
  if (g_stdout_pipe_read < 0) return;
  
  char buf[256];
  ssize_t n;
  
  // Read data from the pipe
  while ((n = read(g_stdout_pipe_read, buf, sizeof(buf))) > 0) {
    // Write to the real stdout
    write(g_real_stdout_fd, buf, n);
    
    // Add to buffer and check for detection
    for (ssize_t i = 0; i < n; i++) {
      g_output_buffer.push_back(buf[i]);
      if (g_output_buffer.size() > OUTPUT_BUFFER_SIZE) {
        g_output_buffer.pop_front();
      }
    }
    
    // Check if the buffer contains "reconnecting" (sdboot's last message)
    if (g_switch_delay_counter == 0 && g_output_buffer.size() >= 12) {
      std::string recent(g_output_buffer.begin(), g_output_buffer.end());
      if (recent.find("reconnecting") != std::string::npos) {
        // Start delay counter to let the HTIF transaction complete
        g_switch_delay_counter = 1;
        dprintf(g_real_stdout_fd, "\nUART-TSI-Interactive: Detected 'reconnecting', waiting for transaction to complete...\n");
      }
    }
  }
  
  // If currently in delay, continue counting
  if (g_switch_delay_counter > 0 && !g_detected_jumping) {
    g_switch_delay_counter++;
    if (g_switch_delay_counter >= SWITCH_DELAY_ITERATIONS) {
      g_detected_jumping = true;
      dprintf(g_real_stdout_fd, "UART-TSI-Interactive: >>> Switching HTIF address NOW! <<<\n");
    }
  }
}

// Signal handler function
static void signal_handler(int sig) {
  g_input_thread_running = false;
  restore_terminal_settings();
  printf("\nInterrupted by signal %d\n", sig);
  exit(128 + sig);
}

// Input thread function
static void* input_thread_func(void* arg) {
  (void)arg;
  char buf[256];
  
  while (g_input_thread_running) {
    // Use select with timeout so we can periodically check whether to exit
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(g_original_stdin, &readfds);
    
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms timeout
    
    int ret = select(g_original_stdin + 1, &readfds, NULL, NULL, &tv);
    if (ret > 0 && FD_ISSET(g_original_stdin, &readfds)) {
      // Input available to read
      ssize_t n = read(g_original_stdin, buf, sizeof(buf));
      if (n > 0) {
        // Echo user input to stdout (if enabled)
        if (g_local_echo) {
          for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\r' || buf[i] == '\n') {
              // Carriage return/newline: print newline
              write(STDOUT_FILENO, "\n", 1);
            } else if (buf[i] == 127 || buf[i] == '\b') {
              // Backspace key: print backspace sequence (backspace+space+backspace)
              write(STDOUT_FILENO, "\b \b", 3);
            } else {
              // Regular character: echo directly
              write(STDOUT_FILENO, &buf[i], 1);
            }
          }
        }
        
        // Write to pipe for fesvr
        ssize_t written = 0;
        while (written < n && g_input_thread_running) {
          ssize_t w = write(g_pipe_write_fd, buf + written, n - written);
          if (w > 0) {
            written += w;
          } else if (w < 0 && errno != EAGAIN && errno != EINTR) {
            break;
          }
        }
      } else if (n == 0) {
        // EOF
        break;
      }
    }
  }
  
  return NULL;
}

// Set up input redirection
static bool setup_input_redirection() {
  // 1. Save the original stdin
  g_original_stdin = dup(STDIN_FILENO);
  if (g_original_stdin < 0) {
    PRINTF("Failed to dup stdin: %s\n", strerror(errno));
    return false;
  }
  
  // 2. Create pipe
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    PRINTF("Failed to create pipe: %s\n", strerror(errno));
    close(g_original_stdin);
    return false;
  }
  
  int pipe_read_fd = pipefd[0];
  g_pipe_write_fd = pipefd[1];
  
  // 3. Redirect stdin to the pipe read end
  if (dup2(pipe_read_fd, STDIN_FILENO) < 0) {
    PRINTF("Failed to redirect stdin: %s\n", strerror(errno));
    close(g_original_stdin);
    close(pipe_read_fd);
    close(g_pipe_write_fd);
    return false;
  }
  close(pipe_read_fd);  // The original read end fd is no longer needed
  
  // 4. Set the original stdin to non-canonical mode (raw mode)
  if (tcgetattr(g_original_stdin, &g_orig_termios) == 0) {
    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);  // Disable canonical mode and echo
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(g_original_stdin, TCSANOW, &raw) == 0) {
      g_terminal_modified = true;
    }
  }
  
  // 5. Start the input thread
  g_input_thread_running = true;
  if (pthread_create(&g_input_thread, NULL, input_thread_func, NULL) != 0) {
    PRINTF("Failed to create input thread: %s\n", strerror(errno));
    g_input_thread_running = false;
    restore_terminal_settings();
    return false;
  }
  
  return true;
}

// Stop input redirection
static void stop_input_redirection() {
  if (g_input_thread_running) {
    g_input_thread_running = false;
    pthread_join(g_input_thread, NULL);
  }
  
  restore_terminal_settings();
  
  if (g_pipe_write_fd >= 0) {
    close(g_pipe_write_fd);
    g_pipe_write_fd = -1;
  }
  
  if (g_original_stdin >= 0) {
    close(g_original_stdin);
    g_original_stdin = -1;
  }
}

testchip_uart_tsi_interactive_t::testchip_uart_tsi_interactive_t(
    int argc, char** argv,
    char* ttyfile, uint64_t baud_rate,
    bool verbose, bool do_self_check)
  : testchip_tsi_t(argc, argv, false), 
    verbose(verbose), 
    in_load_program(false), 
    do_self_check(do_self_check),
    terminal_modified(false) {

  uint64_t baud_sel = B115200;
  switch (baud_rate) {
  case 1200: baud_sel    = B1200; break;
  case 1800: baud_sel    = B1800; break;
  case 2400: baud_sel    = B2400; break;
  case 4800: baud_sel    = B4800; break;
  case 9600: baud_sel    = B9600; break;
  case 19200: baud_sel   = B19200; break;
  case 38400: baud_sel   = B38400; break;
  case 57600: baud_sel   = B57600; break;
  case 115200: baud_sel  = B115200; break;
  case 230400: baud_sel  = B230400; break;
  case 460800: baud_sel  = B460800; break;
  case 500000: baud_sel  = B500000; break;
  case 576000: baud_sel  = B576000; break;
  case 921600: baud_sel  = B921600; break;
  case 1000000: baud_sel = B1000000; break;
  case 1152000: baud_sel = B1152000; break;
  case 1500000: baud_sel = B1500000; break;
  case 2000000: baud_sel = B2000000; break;
  case 2500000: baud_sel = B2500000; break;
  case 3000000: baud_sel = B3000000; break;
  case 4000000: baud_sel = B4000000; break;
  default:
    PRINTF("Unsupported baud rate %ld\n", baud_rate);
    exit(1);
  }

  if (baud_sel != B115200) {
    PRINTF("Warning: You selected a non-standard baudrate. This will only work if the HW was configured with this baud-rate\n");
  }

  ttyfd = open(ttyfile, O_RDWR);
  if (ttyfd < 0) {
    PRINTF("Error %i from open: %s\n", errno, strerror(errno));
    exit(1);
  }

  // Configure UART
  struct termios tty;
  if (tcgetattr(ttyfd, &tty) != 0) {
    PRINTF("Error %i from tcgetaddr: %s\n", errno, strerror(errno));
    exit(1);
  }

  tty.c_cflag &= ~PARENB;
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;
  tty.c_cflag &= ~CRTSCTS;
  tty.c_cflag |= CREAD | CLOCAL;

  tty.c_lflag &= ~ICANON;
  tty.c_lflag &= ~ECHO;
  tty.c_lflag &= ~ECHOE;
  tty.c_lflag &= ~ECHONL;
  tty.c_lflag &= ~ISIG;

  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL);

  tty.c_oflag &= ~OPOST;
  tty.c_oflag &= ~ONLCR;

  tty.c_cc[VTIME] = 0;
  tty.c_cc[VMIN] = 0;

  cfsetispeed(&tty, baud_sel);
  cfsetospeed(&tty, baud_sel);

  if (tcsetattr(ttyfd, TCSANOW, &tty) != 0) {
    PRINTF("Error %i from tcsetattr: %s\n", errno, strerror(errno));
  }

  // Set up global pointer for signal handling
  g_tsi = this;
}

testchip_uart_tsi_interactive_t::~testchip_uart_tsi_interactive_t() {
  restore_terminal();
  g_tsi = nullptr;
}

void testchip_uart_tsi_interactive_t::setup_terminal() {
  // Set up signal handling
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
}

void testchip_uart_tsi_interactive_t::restore_terminal() {
  // Handled by global function
}

bool testchip_uart_tsi_interactive_t::handle_uart() {
  std::vector<uint16_t> to_write;
  while (data_available()) {
     uint32_t d = recv_word();
     to_write.push_back(d);
     to_write.push_back(d >> 16);
  }

  uint8_t* buf = (uint8_t*) to_write.data();
  size_t write_size = to_write.size() * 2;
  size_t written = 0;
  size_t remaining = write_size;

  while (remaining > 0) {
    written = write(ttyfd, buf + write_size - remaining, remaining);
    remaining = remaining - written;
  }
  if (verbose) {
    for (size_t i = 0; i < to_write.size() * 2; i++) {
      PRINTF("Wrote %x\n", buf[i]);
    }
  }

  uint8_t read_buf[256];
  int n = read(ttyfd, &read_buf, sizeof(read_buf));
  if (n < 0) {
    PRINTF("Error %i from read: %s\n", errno, strerror(errno));
    exit(1);
  }
  for (int i = 0; i < n; i++) {
    read_bytes.push_back(read_buf[i]);
  }

  if (read_bytes.size() >= 4) {
    uint32_t out_data = 0;
    uint8_t* b = ((uint8_t*)&out_data);
    for (int i = 0; i < (sizeof(uint32_t) / sizeof(uint8_t)); i++) {
      b[i] = read_bytes.front();
      read_bytes.pop_front();
    }
    if (verbose) PRINTF("Read %x\n", out_data);
    send_word(out_data);
  }
  return data_available() || n > 0;
}

bool testchip_uart_tsi_interactive_t::check_connection() {
  sleep(1);
  uint8_t rdata = 0;
  int n = read(ttyfd, &rdata, 1);
  if (n > 0) {
    PRINTF("Error: Reading unexpected data %c from UART. Abort.\n", rdata);
    exit(1);
  }
  return true;
}

void testchip_uart_tsi_interactive_t::load_program() {
  if (g_attach_mode) {
    PRINTF("Attach mode: skipping program load\n");
    return;
  }
  in_load_program = true;
  PRINTF("Loading program\n");
  testchip_tsi_t::load_program();
  PRINTF("Done loading program\n");
  in_load_program = false;
  
  // Display current HTIF addresses (read from ELF)
  PRINTF("Using tohost=0x%lx, fromhost=0x%lx (from ELF)\n", 
         get_tohost_addr(), get_fromhost_addr());
  
  // Save alternate address info without overriding immediately
  if (g_override_tohost != 0) {
    PRINTF("Will switch to tohost=0x%lx after idle\n", g_override_tohost);
  }
}

void testchip_uart_tsi_interactive_t::write_chunk(addr_t taddr, size_t nbytes, const void* src) {
  if (this->in_load_program) { PRINTF("Loading ELF %lx-%lx ... ", taddr, taddr + nbytes); }
  testchip_tsi_t::write_chunk(taddr, nbytes, src);
  while (this->handle_uart()) { }
  if (this->in_load_program) { printf("Done\n"); }

  if (this->do_self_check && this->in_load_program) {
    uint8_t rbuf[chunk_max_size()];
    const uint8_t* csrc = (const uint8_t*)src;
    PRINTF("Performing self check of region %lx-%lx ... ", taddr, taddr + nbytes);
    read_chunk(taddr, nbytes, rbuf);
    for (size_t i = 0; i < nbytes; i++) {
      if (rbuf[i] != csrc[i]) {
        PRINTF("\nSelf check failed at address %lx readback %x != source %x\n", taddr + i, rbuf[i], csrc[i]);
        while (handle_uart()) { }
        exit(1);
      }
    }
    printf("Done\n");
  }
}

int main(int argc, char* argv[]) {
  PRINTF("Starting Interactive UART-based TSI\n");
  PRINTF("This version supports REAL-TIME user input\n");
  PRINTF("\n");
  PRINTF("Usage: ./uart_tsi_interactive +tty=/dev/ttyxx [options] [<bin>]\n");
  PRINTF("Options:\n");
  PRINTF("  +noecho          Disable local echo (use when target echoes, e.g. Linux)\n");
  PRINTF("  +baudrate=N      Set baud rate (default 115200)\n");
  PRINTF("  +attach          Attach to running system without loading program\n");
  PRINTF("  +tohost=ADDR     Override tohost address (hex, e.g. 0x80041a78)\n");
  PRINTF("  +fromhost=ADDR   Override fromhost address (hex, e.g. 0x80041a70)\n");
  PRINTF("Press Ctrl+C to exit\n");
  PRINTF("\n");

  // ============================================
  // Key: set up input redirection before fesvr initialization
  // ============================================
  if (!setup_input_redirection()) {
    PRINTF("ERROR: Failed to setup input redirection\n");
    exit(1);
  }
  PRINTF("Input redirection setup complete (stdin -> pipe -> input thread)\n");

  std::vector<std::string> args;
  for (int i = 0; i < argc; i++) {
    bool is_plusarg = argv[i][0] == '+';
    if (is_plusarg) {
      args.push_back("+permissive");
      args.push_back(std::string(argv[i]));
      args.push_back("+permissive-off");
    } else {
      args.push_back(std::string(argv[i]));
    }
  }

  std::string tty;
  bool verbose = false;
  bool self_check = false;
  bool attach_mode = false;
  uint64_t baud_rate = 115200;
  for (std::string& arg : args) {
    if (arg.find("+tty=") == 0) {
      tty = std::string(arg.c_str() + 5);
    }
    if (arg.find("+verbose") == 0) {
      verbose = true;
    }
    if (arg.find("+selfcheck") == 0) {
      self_check = true;
    }
    if (arg.find("+baudrate=") == 0) {
      baud_rate = strtoull(arg.substr(10).c_str(), 0, 10);
    }
    if (arg.find("+noecho") == 0) {
      g_local_echo = false;
      PRINTF("Local echo disabled (target will echo)\n");
    }
    if (arg.find("+attach") == 0) {
      attach_mode = true;
      g_attach_mode = true;
      PRINTF("Attach mode: skipping program load, connecting to running system\n");
    }
    if (arg.find("+tohost=") == 0) {
      g_override_tohost = strtoull(arg.substr(8).c_str(), 0, 0);
      PRINTF("Override tohost address: 0x%lx\n", g_override_tohost);
    }
    if (arg.find("+fromhost=") == 0) {
      g_override_fromhost = strtoull(arg.substr(10).c_str(), 0, 0);
      PRINTF("Override fromhost address: 0x%lx\n", g_override_fromhost);
    }
  }

  if (tty.size() == 0) {
    PRINTF("ERROR: Must use +tty=/dev/ttyxx to specify a tty\n");
    stop_input_redirection();
    exit(1);
  }

  // Key: set up stdout interception before creating the tsi object
  // because the syscall_t constructor will dup(1) to save stdout
  bool use_jumping_detection = (g_override_tohost != 0);
  if (use_jumping_detection) {
    if (!setup_stdout_interception()) {
      fprintf(stderr, "WARNING: Failed to setup stdout interception\n");
      use_jumping_detection = false;
    }
  }

  // In attach mode, add dummy.elf as binary (it won't actually be loaded)
  if (attach_mode) {
    // Find the path to dummy.elf (same directory as the executable)
    std::string exe_path = args[0];
    size_t last_slash = exe_path.rfind('/');
    std::string dummy_path;
    if (last_slash != std::string::npos) {
      dummy_path = exe_path.substr(0, last_slash + 1) + "dummy.elf";
    } else {
      dummy_path = "dummy.elf";
    }
    args.push_back(dummy_path);
    PRINTF("Using dummy binary for attach mode: %s\n", dummy_path.c_str());
  }

  PRINTF("Attempting to open TTY at %s\n", tty.c_str());
  std::vector<std::string> tsi_args(args);
  char* tsi_argv[args.size()];
  for (size_t i = 0; i < args.size(); i++)
    tsi_argv[i] = tsi_args[i].data();

  testchip_uart_tsi_interactive_t tsi(args.size(), tsi_argv,
                                       tty.data(), baud_rate,
                                       verbose, self_check);
  
  PRINTF("Checking connection status with %s\n", tty.c_str());
  if (!tsi.check_connection()) {
    PRINTF("Connection failed\n");
    stop_input_redirection();
    exit(1);
  } else {
    PRINTF("Connection succeeded\n");
  }

  // Set terminal to interactive mode
  tsi.setup_terminal();
  PRINTF("============================================================\n");
  if (attach_mode) {
    PRINTF("Attached to running system. Press Ctrl+C to detach.\n");
  } else {
    PRINTF("Interactive mode ready. Type your input when prompted.\n");
  }
  PRINTF("============================================================\n");
  fflush(stdout);

  // Main loop
  bool switched_to_alt = false;
  
  if (attach_mode) {
    // Attach mode: keep running until Ctrl+C
    while (g_input_thread_running) {
      tsi.switch_to_host();
      tsi.handle_uart();
      if (use_jumping_detection) {
        fflush(stdout);
        process_intercepted_stdout();
      }
    }
  } else {
    // Normal mode: run until the program ends
    while (!tsi.done()) {
      tsi.switch_to_host();
      tsi.handle_uart();
      
      // Process intercepted output and detect "Jumping"
      if (use_jumping_detection) {
        fflush(stdout);  // Ensure output is written to the pipe
        process_intercepted_stdout();
      }
      
      // If "Jumping" detected and not yet switched, switch addresses immediately
      if (g_override_tohost != 0 && !switched_to_alt && g_detected_jumping) {
        // Switch to OpenSBI address immediately
        dprintf(g_real_stdout_fd, "UART-TSI-Interactive: Switching to OpenSBI HTIF address...\n");
        dprintf(g_real_stdout_fd, "UART-TSI-Interactive:   tohost:   0x%lx -> 0x%lx\n", 
                tsi.get_tohost_addr(), g_override_tohost);
        dprintf(g_real_stdout_fd, "UART-TSI-Interactive:   fromhost: 0x%lx -> 0x%lx\n", 
                tsi.get_fromhost_addr(), g_override_fromhost);
        tsi.set_tohost_addr(g_override_tohost);
        tsi.set_fromhost_addr(g_override_fromhost);
        switched_to_alt = true;
      }
    }
  }
  
  // Restore stdout
  if (use_jumping_detection) {
    restore_stdout();
  }

  PRINTF("============================================================\n");
  PRINTF("Session ended. Cleaning up...\n");
  
  while (tsi.handle_uart()) {
    tsi.switch_to_host();
  }
  
  stop_input_redirection();
  
  if (!attach_mode) {
    PRINTF("WARNING: You should probably reset the target before running this program again\n");
  }
  return attach_mode ? 0 : tsi.exit_code();
}
