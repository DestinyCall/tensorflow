/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include <string>

#include "flatbuffers/flexbuffers.h"  // from @flatbuffers
#include "tensorflow/lite/c/builtin_op_data.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/core/api/flatbuffer_conversions.h"
#include "tensorflow/lite/core/subgraph.h"
#include "tensorflow/lite/experimental/resource/lookup_interfaces.h"
#include "tensorflow/lite/kernels/kernel_util.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace tflite {
namespace ops {
namespace custom {
namespace hashtable {

static constexpr int kResourceHandleTensor = 0;
static constexpr const char kSharedNameStr[] = "shared_name";
static constexpr const char kKeyDtypeStr[] = "key_dtype";
static constexpr const char kValueDtypeStr[] = "value_dtype";

// TODO(b/144728911): The following structure should be moved to
// builtin_op_data.h when it is ready to become a builtin op.
typedef struct {
  std::string table_name;
  TfLiteType key_dtype;
  TfLiteType value_dtype;
} TfLiteHashtableParams;

void* InitHashtable(TfLiteContext* context, const char* buffer, size_t length) {
  TFLITE_CHECK(buffer != nullptr);

  const uint8_t* buffer_t = reinterpret_cast<const uint8_t*>(buffer);
  const flexbuffers::Map& m = flexbuffers::GetRoot(buffer_t, length).AsMap();
  const std::string table_name = m[kSharedNameStr].AsString().str();

  TfLiteType key_dtype, value_dtype;
  ConvertTensorType(static_cast<TensorType>(m[kKeyDtypeStr].AsInt32()),
                    &key_dtype, nullptr);
  ConvertTensorType(static_cast<TensorType>(m[kValueDtypeStr].AsInt32()),
                    &value_dtype, nullptr);

  TfLiteHashtableParams* option = new TfLiteHashtableParams;
  option->table_name = table_name;
  option->key_dtype = key_dtype;
  option->value_dtype = value_dtype;

  return option;
}

void FreeHashtable(TfLiteContext* context, void* buffer) {
  delete reinterpret_cast<TfLiteHashtableParams*>(buffer);
}

TfLiteStatus PrepareHashtable(TfLiteContext* context, TfLiteNode* node) {
  TF_LITE_ENSURE_EQ(context, NumInputs(node), 0);
  TF_LITE_ENSURE_EQ(context, NumOutputs(node), 1);

  TF_LITE_ENSURE(context, node->user_data != nullptr);
  const auto* params =
      reinterpret_cast<const TfLiteHashtableParams*>(node->user_data);

  TF_LITE_ENSURE(context, !params->table_name.empty());
  TF_LITE_ENSURE(context, (params->key_dtype == kTfLiteInt64 &&
                           params->value_dtype == kTfLiteString) ||
                              (params->key_dtype == kTfLiteString &&
                               params->value_dtype == kTfLiteInt64));

  TfLiteTensor* resource_handle_tensor;
  TF_LITE_ENSURE_OK(context, GetOutputSafe(context, node, kResourceHandleTensor,
                                           &resource_handle_tensor));
  TF_LITE_ENSURE(context, resource_handle_tensor->type == kTfLiteResource ||
                              resource_handle_tensor->type == kTfLiteInt32);

  // Resource tensor buffer as a hash table handler will have an 32-bit integer
  // identity.
  size_t bytesRequired = sizeof(int32_t);
  resource_handle_tensor->bytes = bytesRequired;
  // Realloc space for an integer handle value.
  TfLiteTensorRealloc(bytesRequired, resource_handle_tensor);

  // Make shape be [1] to store one integer value.
  TfLiteIntArray* outputSize = TfLiteIntArrayCreate(1);
  outputSize->data[0] = 1;
  if (resource_handle_tensor->dims)
    TfLiteIntArrayFree(resource_handle_tensor->dims);
  resource_handle_tensor->dims = outputSize;
  return kTfLiteOk;
}

TfLiteStatus EvalHashtable(TfLiteContext* context, TfLiteNode* node) {
  TF_LITE_ENSURE(context, node->user_data != nullptr);
  const auto* params =
      reinterpret_cast<const TfLiteHashtableParams*>(node->user_data);

  // The resource id is generated based on the given table name.
  const int32_t resource_id = std::hash<std::string>{}(params->table_name);

  TfLiteTensor* resource_handle_tensor;
  TF_LITE_ENSURE_OK(context, GetOutputSafe(context, node, kResourceHandleTensor,
                                           &resource_handle_tensor));
  *resource_handle_tensor->data.i32 = resource_id;

  Subgraph* subgraph = reinterpret_cast<Subgraph*>(context->impl_);
  auto& resources = subgraph->resources();
  resource::CreateHashtableResourceIfNotAvailable(
      &resources, resource_id, params->key_dtype, params->value_dtype);
  return kTfLiteOk;
}

}  // namespace hashtable

TfLiteRegistration* Register_HASHTABLE() {
  static TfLiteRegistration r = {
      hashtable::InitHashtable, hashtable::FreeHashtable,
      hashtable::PrepareHashtable, hashtable::EvalHashtable};
  return &r;
}

}  // namespace custom
}  // namespace ops
}  // namespace tflite
