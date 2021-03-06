// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#if defined(USE_NCCL) || defined(USE_HOROVOD)

#pragma once
#include "core/common/common.h"
#include "core/providers/cuda/cuda_common.h"

namespace onnxruntime {
namespace cuda {

class Recv final : public CudaKernel {
public:
  Recv(const OpKernelInfo& info) : CudaKernel(info) {
    ORT_ENFORCE(info.GetAttr<int64_t>("tag", &tag_).IsOK());
    ORT_ENFORCE(info.GetAttrs<int64_t>("element_types", element_types_).IsOK());
  }

  Status ComputeInternal(OpKernelContext* context) const override;

private:
  int64_t tag_;
  std::vector<int64_t> element_types_;
};

}  // namespace cuda
}  // namespace onnxruntime

#endif