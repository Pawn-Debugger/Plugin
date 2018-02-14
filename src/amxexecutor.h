#ifndef AMXEXECUTOR_H
#define AMXEXECUTOR_H

#include <atomic>

#include <amx/amx.h>
#include <amx/osdefs.h>

#include "amxservice.h"

class AMXExecutor : public AMXService<AMXExecutor> {
 friend class AMXService<AMXExecutor>;

 public:
  int HandleAMXExec(cell *retval, int index);
  bool get_stopped() const { return stopped_; };
  void set_stopped(bool value) { stopped_ = value; };

 private:
  AMXExecutor(AMX *amx);

  std::atomic<bool> stopped_ = true;
};

#endif // !AMXEXECUTOR_H