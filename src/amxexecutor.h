#ifndef AMXEXECUTOR_H
#define AMXEXECUTOR_H

#include <amx/amx.h>
#include <amx/osdefs.h>

#include "amxservice.h"

class AMXExecutor : public AMXService<AMXExecutor> {
 friend class AMXService<AMXExecutor>;

 public:
  int HandleAMXExec(cell *retval, int index);

 private:
  AMXExecutor(AMX *amx);
};

#endif // !AMXEXECUTOR_H