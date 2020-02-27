// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "mixed_precision_scale.h"

using namespace ONNX_NAMESPACE;
using namespace onnxruntime::common;
namespace onnxruntime {
namespace hip {

#define REGISTER_MIXEDPRECISIONSCALE_KERNEL_TYPED(SrcT)                         \
  ONNX_OPERATOR_TYPED_KERNEL_EX(                                                \
      MixedPrecisionScale,                                                      \
      kOnnxDomain,                                                              \
      9,                                                                        \
      SrcT,                                                                     \
      kHipExecutionProvider,                                                   \
      KernelDefBuilder()                                                        \
          .TypeConstraint("SrcT", DataTypeImpl::GetTensorType<SrcT>())          \
          .TypeConstraint("ScaleT", DataTypeImpl::GetTensorType<float>())       \
          .TypeConstraint("DstT", DataTypeImpl::AllIEEEFloatTensorTypes()), \
      MixedPrecisionScale<SrcT>);

Status BytesPerElement(ONNX_NAMESPACE::TensorProto_DataType to, size_t& bytes_per_elem) {
  switch (to) {
    case ONNX_NAMESPACE::TensorProto_DataType_DOUBLE:
      bytes_per_elem = sizeof(double);
      break;
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT:
      bytes_per_elem = sizeof(float);
      break;
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT16:
      bytes_per_elem = sizeof(MLFloat16);
      break;
    default:
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Unexpected 'to' argument value: ", to);
  }
  return Status::OK();
}

template <typename SrcT>
MixedPrecisionScale<SrcT>::MixedPrecisionScale(const OpKernelInfo& info) : HipKernel(info) {
  int64_t to;
  Status status = info.GetAttr("to", &to);
  ORT_ENFORCE(status.IsOK(), "Attribute to is not set.");
  to_ = gsl::narrow_cast<ONNX_NAMESPACE::TensorProto_DataType>(to);

  status = BytesPerElement(to_, bytes_per_output_elem_);
  ORT_ENFORCE(status.IsOK(), status.ErrorMessage());

  int64_t fuse_outputs;
  info.GetAttrOrDefault("fuse_outputs", &fuse_outputs, static_cast<int64_t>(0));
  fuse_outputs_ = (fuse_outputs != 0);
}

template <typename SrcT>
Status MixedPrecisionScale<SrcT>::ComputeInternal(OpKernelContext* context) const {
  typedef typename ToHipType<SrcT>::MappedType HipSrcT;

  const Tensor* scale = context->Input<Tensor>(0);
  const float* scale_data = scale->template Data<float>();

  // prepare outputs
  int num_inputs = context->InputCount() - 1;
  std::vector<void*> y_datas(num_inputs);
  if (fuse_outputs_) {
    int64_t total_num_elems = 0;
    std::vector<size_t> y_byte_offsets(num_inputs);
    for (int i = 0; i < num_inputs; ++i) {
      const Tensor* X = context->Input<Tensor>(i + 1);
      y_byte_offsets[i] = total_num_elems * bytes_per_output_elem_;
      total_num_elems += X->Shape().Size();
    }

    Tensor* Y = context->Output(0, {total_num_elems});
    void* y_data = Y->MutableDataRaw();
    for (int i = 0; i < num_inputs; ++i) {
      y_datas[i] = (int8_t*)y_data + y_byte_offsets[i];
    }
  } else {
    for (int i = 0; i < num_inputs; ++i) {
      const Tensor* X = context->Input<Tensor>(i + 1);
      Tensor* Y = context->Output(i, X->Shape());
      y_datas[i] = Y->MutableDataRaw();
    }
  }

#define CASE(TP_TYPE, DstT)                                                    \
  case TP_TYPE:                                                                \
    Impl_MixedPrecisionScale<HipSrcT, typename ToHipType<DstT>::MappedType>( \
        x_data,                                                                \
        scale_data,                                                            \
        reinterpret_cast<typename ToHipType<DstT>::MappedType*>(y_data),      \
        count);                                                                \
    break;

  for (int i = 0; i < num_inputs; ++i) {
    const Tensor* X = context->Input<Tensor>(i + 1);
    size_t count = X->Shape().Size();
    const HipSrcT* x_data = reinterpret_cast<const HipSrcT*>(X->template Data<SrcT>());
    auto y_data = y_datas[i];

    switch (to_) {
      CASE(TensorProto_DataType_FLOAT16, MLFloat16)
      CASE(TensorProto_DataType_FLOAT, float)
      default:
        return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Unexpected 'to' argument value: ", to_);
    }
  }

  return Status::OK();
}

REGISTER_MIXEDPRECISIONSCALE_KERNEL_TYPED(MLFloat16)
REGISTER_MIXEDPRECISIONSCALE_KERNEL_TYPED(float)

template Status MixedPrecisionScale<MLFloat16>::ComputeInternal(OpKernelContext* context) const;
template Status MixedPrecisionScale<float>::ComputeInternal(OpKernelContext* context) const;

}  // namespace hip
}  // namespace onnxruntime