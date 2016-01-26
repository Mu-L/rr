/* -*- Mode: C++; tab-width: 8; c-basic-offset: 2; indent-tabs-mode: nil; -*- */

#ifndef RR_GDB_COMMAND_HANDLER_H_
#define RR_GDB_COMMAND_HANDLER_H_

#include <string>

class GdbCommand;
class GdbServer;
class Task;

class GdbCommandHandler {
public:
  // Declare any registered command with supporting
  // wrapper code.
  static std::string gdb_macros();

  static void register_command(GdbCommand& cmd);

  /**
   * Process an incoming GDB payload of the following form:
   *   <command name>:<arg1>:<arg2>:...
   *
   * NOTE: RR Command are typically sent with the qRRCmd: prefix which
   * should of been striped already.
   */
  static std::string process_command(GdbServer& gdb_server, Task* t,
                                     std::string payload);

private:
};

#endif
