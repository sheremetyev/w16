#ifndef V8_STM_H_
#define V8_STM_H_

#include "globals.h"

namespace v8 {
namespace internal {

class STM {
public:

private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(STM);

  Isolate* isolate_;

  friend class Isolate;
};

} } // namespace v8::internal

#endif // V8_STM_H_
