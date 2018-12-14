//  Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
 // LICENSE file in the root directory of this source tree.

#pragma once

#include <jsi.h>
#include <memory.h>

namespace facebook {
namespace v8runtime {

std::unique_ptr<jsi::Runtime> makeV8Runtime();

} // namespace v8runtime
} // namespace facebook
