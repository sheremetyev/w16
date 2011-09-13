#include "v8.h"
#include "stm.h"
#include "isolate.h"

namespace v8 {
namespace internal {

STM::STM() : heap_mutex_(OS::CreateMutex()) {
}

void STM::EnterAllocationScope() {
  heap_mutex_->Lock();
}

void STM::LeaveAllocationScope() {
  heap_mutex_->Unlock();
}

void STM::EnterCollectionScope() {
  heap_mutex_->Lock();
}

void STM::LeaveCollectionScope() {
  heap_mutex_->Unlock();
}

} } // namespace v8::internal
