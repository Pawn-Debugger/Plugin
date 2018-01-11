// Copyright (c) 2012-2015 Zeex
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

#include "amxcallstack.h"

AMXCall::AMXCall(Type type, AMXScript amx, cell index)
 : amx_(amx),
   type_(type),
   frm_(amx.GetFrm()),
   cip_(amx.GetCip()),
   index_(index)
{
}

AMXCall::AMXCall(Type type, AMXScript amx, cell index, cell frm, cell cip)
 : amx_(amx),
   type_(type),
   frm_(frm),
   cip_(cip),
   index_(index)
{
}

// static
AMXCall AMXCall::Public(AMXScript amx, cell index) {
  return AMXCall(PUBLIC, amx, index);
}

// static
AMXCall AMXCall::Native(AMXScript amx, cell index) {
  return AMXCall(NATIVE, amx, index);
}

bool AMXCallStack::IsEmpty() const {
  return call_stack_.empty();
}

AMXCall &AMXCallStack::Top() {
  assert(!IsEmpty());
  return call_stack_.top();
}

const AMXCall &AMXCallStack::Top() const {
  assert(!IsEmpty());
  return call_stack_.top();
}

void AMXCallStack::Push(AMXCall call) {
  call_stack_.push(call);
}

AMXCall AMXCallStack::Pop() {
  assert(!IsEmpty());
  AMXCall result = call_stack_.top();
  call_stack_.pop();
  return result;
}
