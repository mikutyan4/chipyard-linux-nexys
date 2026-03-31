#ifndef __TESTCHIP_UART_TSI_INTERACTIVE_H
#define __TESTCHIP_UART_TSI_INTERACTIVE_H

#include "testchip_tsi.h"
#include <termios.h>

/**
 * Interactive UART TSI - version with standard input support
 *
 * Differences from the original uart_tsi:
 * - Supports reading user input from the terminal
 * - Forwards input to HTIF's stdin
 */
class testchip_uart_tsi_interactive_t : public testchip_tsi_t
{
public:
  testchip_uart_tsi_interactive_t(int argc, char** argv, char* tty,
                                   uint64_t baud_rate,
                                   bool verbose, bool do_self_check);
  virtual ~testchip_uart_tsi_interactive_t();

  bool handle_uart();
  bool check_connection();
  void load_program() override;
  void write_chunk(addr_t taddr, size_t nbytes, const void* src) override;
  
  // New: handle terminal input
  void setup_terminal();
  void restore_terminal();

private:
  int ttyfd;
  std::deque<uint8_t> read_bytes;
  bool verbose;
  bool in_load_program;
  bool do_self_check;
  
  // New: saved terminal state
  struct termios orig_termios;
  bool terminal_modified;
};

#endif
