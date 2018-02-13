// Copyright (c) 2011-2015 Zeex
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <configreader.h>

#include "amxcallstack.h"
#include "amxdebuginfo.h"
#include "amxerror.h"
#include "amxexecutor.h"
#include "amxopcode.h"
#include "amxpathfinder.h"
#include "amxscript.h"
#include "amxstacktrace.h"
#include "debugplugin.h"
#include "fileutils.h"
#include "log.h"
#include "os.h"
#include "stacktrace.h"

#define AMX_EXEC_GDK    (-10)
#define AMX_EXEC_GDK_42 (-10000)

namespace {

ConfigReader server_cfg("server.cfg");

class HexDword {
 public:
  static const int kWidth = 8;
  HexDword(uint32_t value): value_(value) {}
  HexDword(void *value): value_(reinterpret_cast<uint32_t>(value)) {}
  friend std::ostream &operator<<(std::ostream &stream, const HexDword &x) {
    char fill = stream.fill();
    stream << std::setw(kWidth) << std::setfill('0') << std::hex
           << x.value_ << std::dec;
    stream.fill(fill);
    return stream;
  }
 private:
  uint32_t value_;
};

int CharToTraceFlag(char c) {
  switch (c) {
    case 'n':
      return DebugPlugin::TRACE_NATIVES;
    case 'p':
      return DebugPlugin::TRACE_PUBLICS;
    case 'f':
      return DebugPlugin::TRACE_FUNCTIONS;
  }
  return 0;
}

int StringToTraceFlags(const std::string &string) {
  int flags = 0;
  for (std::size_t i = 0; i < string.length(); i++) {
    flags |= CharToTraceFlag(string[i]);
  }
  return flags;
}

template<typename Func>
void SplitString(const std::string &s, char delim, Func func) {
  std::string::size_type begin = 0;
  std::string::size_type end;

  while (begin < s.length()) {
    end = s.find(delim, begin);
    end = (end == std::string::npos) ? s.length() : end;
    func(std::string(s.begin() + begin,
                     s.begin() + end));
    begin = end + 1;
  }
}

template<typename FormattedPrinter>
class PrintLine : public std::function<void(const std::string &)> {
 public:
  PrintLine(FormattedPrinter printer) : printer_(printer) {}
  void operator()(const std::string &line) {
    printer_("%s", line.c_str());
  }
 private:
  FormattedPrinter printer_;
};

template<typename FormattedPrinter>
void PrintStream(FormattedPrinter printer, const std::stringstream &stream) {
  SplitString(stream.str(), '\n', PrintLine<FormattedPrinter>(printer));
}

} // anonymous namespace

int DebugPlugin::trace_flags_(StringToTraceFlags(
  server_cfg.GetValueWithDefault("trace")));
RegExp DebugPlugin::trace_filter_(
  server_cfg.GetValueWithDefault("trace_filter", ".*"));

AMXCallStack DebugPlugin::call_stack_;

DebugPlugin::DebugPlugin(AMX *amx)
 : AMXService<DebugPlugin>(amx),
   prev_debug_(0),
   prev_callback_(0),
   last_frame_(amx->stp),
   block_exec_errors_(false)
{
}

int DebugPlugin::Load() {
  AMXPathFinder amx_finder;
  amx_finder.AddSearchPath("gamemodes");
  amx_finder.AddSearchPath("filterscripts");

  const char *var = getenv("AMX_PATH");
  if (var != 0) {
    SplitString(var, fileutils::kNativePathListSepChar,
        std::bind(std::mem_fn(&AMXPathFinder::AddSearchPath), &amx_finder, std::placeholders::_1));
  }

  amx_path_ = amx_finder.Find(amx());
  if (!amx_path_.empty()) {
    if (AMXDebugInfo::IsPresent(amx())) {
      debug_info_.Load(amx_path_);
    }
  }

  amx_name_ = fileutils::GetFileName(amx_path_);
  if (amx_name_.empty()) {
    assert(amx_path_.empty());
    amx_name_ = "<unknown>";
  }

  amx().DisableSysreqD();
  prev_debug_ = amx().GetDebugHook();
  prev_callback_ = amx().GetCallback();

  return AMX_ERR_NONE;
}

int DebugPlugin::Unload() {
  return AMX_ERR_NONE;
}

int DebugPlugin::HandleAMXDebug() {
  if (amx().GetFrm() < last_frame_ && (trace_flags_ & TRACE_FUNCTIONS)
      && debug_info_.IsLoaded()) {
    AMXStackTrace trace =
      GetAMXStackTrace(amx(), amx().GetFrm(), amx().GetCip(), 1);
    if (trace.current_frame().return_address() != 0) {
      PrintTraceFrame(trace.current_frame(), debug_info_);
    }
  }
  last_frame_ = amx().GetFrm();
  return prev_debug_ != 0 ? prev_debug_(amx()) : AMX_ERR_NONE;
}

int DebugPlugin::HandleAMXCallback(cell index, cell *result, cell *params) {
  call_stack_.Push(AMXCall::Native(amx(), index));

  if (trace_flags_ & TRACE_NATIVES) {
    std::stringstream stream;
    const char *name = amx().GetNativeName(index);
    stream << "native " << (name != 0 ? name : "<unknown>") << " ()";
    if (trace_filter_.Test(stream.str())) {
      PrintStream(LogTracePrint, stream);
    }
  }

  int error = prev_callback_(amx(), index, result, params);

  call_stack_.Pop();
  return error;
}

void DebugPlugin::HandleAMXExecError(int index,
                                     cell *retval,
                                     const AMXError &error) {
  if (block_exec_errors_) {
    return;
  }

  // The following error codes should not be treated as errors:
  //
  // 1. AMX_ERR_NONE is always returned when a public function returns
  //    normally as it jumps to return address 0 where it performs "halt 0".
  //
  // 2. AMX_ERR_SLEEP is returned when the VM is put into sleep mode. The
  //    execution can be later continued using AMX_EXEC_CONT.
  if (error.code() == AMX_ERR_NONE || error.code() == AMX_ERR_SLEEP) {
    return;
  }

  // For compatibility with sampgdk.
  if (error.code() == AMX_ERR_INDEX && (index == AMX_EXEC_GDK ||
                                        index <= AMX_EXEC_GDK_42)) {
    return;
  }

  // Block errors while calling OnRuntimeError as it may result in yet
  // another error (and for certain errors it in fact always does, e.g.
  // stack/heap collision due to insufficient stack space for making
  // the public call).
  block_exec_errors_ = true;

  // Capture backtrace before continuing as OnRuntimError will modify the
  // state of the AMX thus we'll end up with a different stack and possibly
  // other things too. This also should protect from cases where something
  // hooks logprintf (like fixes2).
  std::stringstream bt_stream;
  PrintAMXBacktrace(bt_stream);

  // public OnRuntimeError(code, &bool:suppress);
  cell callback_index = amx().GetPublicIndex("OnRuntimeError");
  cell suppress = 0;

  if (callback_index >= 0) {
    if (amx().IsStackOK()) {
      cell suppress_addr, *suppress_ptr;
      amx_PushArray(amx(), &suppress_addr, &suppress_ptr, &suppress, 1);
      amx_Push(amx(), error.code());
      AMXExecutor::GetInstance(amx())->HandleAMXExec(retval, callback_index);
      amx_Release(amx(), suppress_addr);
      suppress = *suppress_ptr;
    }
  }

  if (suppress == 0) {
    PrintRuntimeError(amx(), error);
    if (error.code() != AMX_ERR_NOTFOUND &&
        error.code() != AMX_ERR_INDEX    &&
        error.code() != AMX_ERR_CALLBACK &&
        error.code() != AMX_ERR_INIT) {
      PrintStream(LogDebugPrint, bt_stream);
    }
  }

  block_exec_errors_ = false;
}

void DebugPlugin::HandleException() {
  LogDebugPrint("Server crashed while executing %s", amx_name_.c_str());
  PrintAMXBacktrace();
}

void DebugPlugin::HandleInterrupt() {
  LogDebugPrint("Server received interrupt signal while executing %s",
                amx_name_.c_str());
  PrintAMXBacktrace();
}

// static
bool DebugPlugin::IsInsideAMX() {
  return !call_stack_.IsEmpty();
}

// static
void DebugPlugin::OnCrash(const os::Context &context) {
  if (IsInsideAMX()) {
    DebugPlugin::GetInstance(call_stack_.Top().amx())->HandleException();
  } else {
    LogDebugPrint("Server crashed due to an unknown error");
  }
  PrintNativeBacktrace(context.native_context());
  PrintRegisters(context);
  PrintStack(context);
  PrintLoadedModules();
}

// static
void DebugPlugin::OnInterrupt(const os::Context &context) {
  if (IsInsideAMX()) {
    DebugPlugin::GetInstance(call_stack_.Top().amx())->HandleInterrupt();
  } else {
    LogDebugPrint("Server received interrupt signal");
  }
  PrintNativeBacktrace(context.native_context());
}

// static
void DebugPlugin::PrintTraceFrame(const AMXStackFrame &frame,
                                  const AMXDebugInfo &debug_info) {
  std::stringstream stream;
  AMXStackFramePrinter printer(stream, debug_info);
  printer.PrintCallerNameAndArguments(frame);
  if (trace_filter_.Test(stream.str())) {
    PrintStream(LogTracePrint, stream);
  }
}

// static
void DebugPlugin::PrintRuntimeError(AMXScript amx, const AMXError &error) {
  LogDebugPrint("Run time error %d: \"%s\"", error.code(), error.GetString());
  cell *ip = reinterpret_cast<cell*>(amx.GetCode() + amx.GetCip());
  switch (error.code()) {
    case AMX_ERR_BOUNDS: {
      cell opcode = *ip;
      if (opcode == RelocateAMXOpcode(AMX_OP_BOUNDS)) {
        cell upper_bound = *(ip + 1);
        cell index = amx.GetPri();
        if (index < 0) {
          LogDebugPrint(" Attempted to read/write array element at negative "
                        "index %d", index);
        } else {
          LogDebugPrint(" Attempted to read/write array element at index %d "
                        "in array of size %d", index, upper_bound + 1);
        }
      }
      break;
    }
    case AMX_ERR_NOTFOUND: {
      const AMX_FUNCSTUBNT *natives = amx.GetNatives();
      int num_natives = amx.GetNumNatives();
      for (int i = 0; i < num_natives; ++i) {
        if (natives[i].address == 0) {
          LogDebugPrint(" %s", amx.GetName(natives[i].nameofs));
        }
      }
      break;
    }
    case AMX_ERR_STACKERR:
      LogDebugPrint(" Stack pointer (STK) is 0x%X, heap pointer (HEA) is 0x%X",
                    amx.GetStk(), amx.GetHea());
      break;
    case AMX_ERR_STACKLOW:
      LogDebugPrint(" Stack pointer (STK) is 0x%X, stack top (STP) is 0x%X",
                    amx.GetStk(), amx.GetStp());
      break;
    case AMX_ERR_HEAPLOW:
      LogDebugPrint(" Heap pointer (HEA) is 0x%X, heap bottom (HLW) is 0x%X",
                    amx.GetHea(), amx.GetHlw());
      break;
    case AMX_ERR_INVINSTR: {
      cell opcode = *ip;
      LogDebugPrint(" Unknown opcode 0x%x at address 0x%08X",
                    opcode , amx.GetCip());
      break;
    }
    case AMX_ERR_NATIVE: {
      cell opcode = *(ip - 2);
      if (opcode == RelocateAMXOpcode(AMX_OP_SYSREQ_C)) {
        cell index = *(ip - 1);
        LogDebugPrint(" %s", amx.GetNativeName(index));
      }
      break;
    }
  }
}

// static
void DebugPlugin::PrintAMXBacktrace() {
  std::stringstream stream;
  PrintAMXBacktrace(stream);
  PrintStream(LogDebugPrint, stream);
}

// static
void DebugPlugin::PrintAMXBacktrace(std::ostream &stream) {
  AMXScript amx = call_stack_.Top().amx();
  AMXScript top_amx = amx;

  stream << "AMX backtrace:";

  AMXCallStack calls = call_stack_;

  cell cip = top_amx.GetCip();
  cell frm = top_amx.GetFrm();
  int level = 0;

  while (!calls.IsEmpty() && cip != 0 && amx == top_amx) {
    AMXCall call = calls.Pop();

    // native function
    if (call.IsNative()) {
      const char *name = amx.GetNativeName(call.index());
      stream << "\n#" << level++
             << " native "
             << (name != 0 ? name : "<unknown>") << " ()";
      std::string module = os::GetModuleName(
        reinterpret_cast<void*>(amx.GetNativeAddress(call.index())));
      if (!module.empty()) {
        stream << " from " << fileutils::GetFileName(module);
      }
    }

    // public function
    else if (call.IsPublic()) {
      DebugPlugin *cd = DebugPlugin::GetInstance(amx);

      AMXStackTrace trace = GetAMXStackTrace(amx, frm, cip, 100);
      std::deque<AMXStackFrame> frames;

      while (trace.current_frame().return_address() != 0) {
        frames.push_back(trace.current_frame());
        if (!trace.MoveNext()) {
          break;
        }
      }

      cell entry_point = amx.GetPublicAddress(call.index());
      if (frames.empty()) {
        AMXStackFrame fake_frame(amx, frm, 0, 0, entry_point);
        frames.push_front(fake_frame);
      } else {
        frames.back().set_caller_address(entry_point);
      }

      for (std::deque<AMXStackFrame>::const_iterator it = frames.begin();
           it != frames.end(); it++) {
        const AMXStackFrame &frame = *it;

        stream << "\n#" << level++ << " ";
        frame.Print(stream, cd->debug_info_);

        if (!cd->debug_info_.IsLoaded()) {
          stream << " from " << cd->amx_name_;
        }
      }

      frm = call.frm();
      cip = call.cip();
    }
  }
}

// static
void DebugPlugin::PrintRegisters(const os::Context &context) {
  os::Context::Registers registers = context.GetRegisters();

  LogDebugPrint("Registers:");
  LogDebugPrint("EAX: %08x EBX: %08x ECX: %08x EDX: %08x",
                registers.eax,
                registers.ebx,
                registers.ecx,
                registers.edx);
  LogDebugPrint("ESI: %08x EDI: %08x EBP: %08x ESP: %08x",
                registers.esi,
                registers.edi,
                registers.ebp,
                registers.esp);
  LogDebugPrint("EIP: %08x EFLAGS: %08x",
                registers.eip,
                registers.eflags);
}

// static
void DebugPlugin::PrintStack(const os::Context &context) {
  os::Context::Registers registers = context.GetRegisters();
  os::uint32_t *stack_ptr = reinterpret_cast<os::uint32_t *>(registers.esp);
  if (stack_ptr == 0) {
    return;
  }

  LogDebugPrint("Stack:");

  for (int i = 0; i < 256; i += 8) {
    LogDebugPrint("ESP+%08x: %08x %08x %08x %08x",
                  i * sizeof(*stack_ptr),
                  stack_ptr[i],
                  stack_ptr[i + 1],
                  stack_ptr[i + 2],
                  stack_ptr[i + 3]);
  }
}

// static
void DebugPlugin::PrintLoadedModules() {
  LogDebugPrint("Loaded modules:");

  std::vector<os::Module> modules;
  os::GetLoadedModules(modules);

  for (std::vector<os::Module>::const_iterator it = modules.begin();
       it != modules.end(); it++) {
    const os::Module &module = *it;
    LogDebugPrint("%08x - %08x %s",
                  module.base_address(),
                  module.base_address() + module.size(),
                  module.name().c_str());
  }
}

// static
void DebugPlugin::PrintNativeBacktrace(const os::Context &context) {
  std::stringstream stream;
  PrintNativeBacktrace(stream, context);
  PrintStream(LogDebugPrint, stream);
}

// static
void DebugPlugin::PrintNativeBacktrace(std::ostream &stream,
                                       const os::Context &context) {
  std::vector<StackFrame> frames;
  GetStackTrace(frames, context.native_context());

  if (!frames.empty()) {
    stream << "Native backtrace:";

    int level = 0;
    for (std::vector<StackFrame>::const_iterator it = frames.begin();
         it != frames.end(); it++) {
      const StackFrame &frame = *it;

      stream << "\n#" << level++ << " ";
      frame.Print(stream);

      std::string module = os::GetModuleName(frame.return_address());
      if (!module.empty()) {
        stream << " from " << fileutils::GetRelativePath(module);
      }
    }
  }
}
