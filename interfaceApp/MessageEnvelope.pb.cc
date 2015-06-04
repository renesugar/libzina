// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: MessageEnvelope.proto

#define INTERNAL_SUPPRESS_PROTOBUF_FIELD_DEPRECATION
#include "MessageEnvelope.pb.h"

#include <algorithm>

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/once.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite_inl.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
// @@protoc_insertion_point(includes)

namespace axolotl {

void protobuf_ShutdownFile_MessageEnvelope_2eproto() {
  delete MessageEnvelope::default_instance_;
}

#ifdef GOOGLE_PROTOBUF_NO_STATIC_INITIALIZER
void protobuf_AddDesc_MessageEnvelope_2eproto_impl() {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

#else
void protobuf_AddDesc_MessageEnvelope_2eproto() {
  static bool already_here = false;
  if (already_here) return;
  already_here = true;
  GOOGLE_PROTOBUF_VERIFY_VERSION;

#endif
  MessageEnvelope::default_instance_ = new MessageEnvelope();
  MessageEnvelope::default_instance_->InitAsDefaultInstance();
  ::google::protobuf::internal::OnShutdown(&protobuf_ShutdownFile_MessageEnvelope_2eproto);
}

#ifdef GOOGLE_PROTOBUF_NO_STATIC_INITIALIZER
GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AddDesc_MessageEnvelope_2eproto_once_);
void protobuf_AddDesc_MessageEnvelope_2eproto() {
  ::google::protobuf::GoogleOnceInit(&protobuf_AddDesc_MessageEnvelope_2eproto_once_,
                 &protobuf_AddDesc_MessageEnvelope_2eproto_impl);
}
#else
// Force AddDescriptors() to be called at static initialization time.
struct StaticDescriptorInitializer_MessageEnvelope_2eproto {
  StaticDescriptorInitializer_MessageEnvelope_2eproto() {
    protobuf_AddDesc_MessageEnvelope_2eproto();
  }
} static_descriptor_initializer_MessageEnvelope_2eproto_;
#endif

// ===================================================================

#ifndef _MSC_VER
const int MessageEnvelope::kNameFieldNumber;
const int MessageEnvelope::kScClientDevIdFieldNumber;
const int MessageEnvelope::kDeviceIdFieldNumber;
const int MessageEnvelope::kSupplementFieldNumber;
const int MessageEnvelope::kMessageFieldNumber;
#endif  // !_MSC_VER

MessageEnvelope::MessageEnvelope()
  : ::google::protobuf::MessageLite() {
  SharedCtor();
  // @@protoc_insertion_point(constructor:axolotl.MessageEnvelope)
}

void MessageEnvelope::InitAsDefaultInstance() {
}

MessageEnvelope::MessageEnvelope(const MessageEnvelope& from)
  : ::google::protobuf::MessageLite() {
  SharedCtor();
  MergeFrom(from);
  // @@protoc_insertion_point(copy_constructor:axolotl.MessageEnvelope)
}

void MessageEnvelope::SharedCtor() {
  ::google::protobuf::internal::GetEmptyString();
  _cached_size_ = 0;
  name_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
  scclientdevid_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
  device_id_ = 0u;
  supplement_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
  message_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
  ::memset(_has_bits_, 0, sizeof(_has_bits_));
}

MessageEnvelope::~MessageEnvelope() {
  // @@protoc_insertion_point(destructor:axolotl.MessageEnvelope)
  SharedDtor();
}

void MessageEnvelope::SharedDtor() {
  if (name_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    delete name_;
  }
  if (scclientdevid_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    delete scclientdevid_;
  }
  if (supplement_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    delete supplement_;
  }
  if (message_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    delete message_;
  }
  #ifdef GOOGLE_PROTOBUF_NO_STATIC_INITIALIZER
  if (this != &default_instance()) {
  #else
  if (this != default_instance_) {
  #endif
  }
}

void MessageEnvelope::SetCachedSize(int size) const {
  GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
  _cached_size_ = size;
  GOOGLE_SAFE_CONCURRENT_WRITES_END();
}
const MessageEnvelope& MessageEnvelope::default_instance() {
#ifdef GOOGLE_PROTOBUF_NO_STATIC_INITIALIZER
  protobuf_AddDesc_MessageEnvelope_2eproto();
#else
  if (default_instance_ == NULL) protobuf_AddDesc_MessageEnvelope_2eproto();
#endif
  return *default_instance_;
}

MessageEnvelope* MessageEnvelope::default_instance_ = NULL;

MessageEnvelope* MessageEnvelope::New() const {
  return new MessageEnvelope;
}

void MessageEnvelope::Clear() {
  if (_has_bits_[0 / 32] & 31) {
    if (has_name()) {
      if (name_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
        name_->clear();
      }
    }
    if (has_scclientdevid()) {
      if (scclientdevid_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
        scclientdevid_->clear();
      }
    }
    device_id_ = 0u;
    if (has_supplement()) {
      if (supplement_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
        supplement_->clear();
      }
    }
    if (has_message()) {
      if (message_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
        message_->clear();
      }
    }
  }
  ::memset(_has_bits_, 0, sizeof(_has_bits_));
  mutable_unknown_fields()->clear();
}

bool MessageEnvelope::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
#define DO_(EXPRESSION) if (!(EXPRESSION)) goto failure
  ::google::protobuf::uint32 tag;
  ::google::protobuf::io::StringOutputStream unknown_fields_string(
      mutable_unknown_fields());
  ::google::protobuf::io::CodedOutputStream unknown_fields_stream(
      &unknown_fields_string);
  // @@protoc_insertion_point(parse_start:axolotl.MessageEnvelope)
  for (;;) {
    ::std::pair< ::google::protobuf::uint32, bool> p = input->ReadTagWithCutoff(127);
    tag = p.first;
    if (!p.second) goto handle_unusual;
    switch (::google::protobuf::internal::WireFormatLite::GetTagFieldNumber(tag)) {
      // optional string name = 1;
      case 1: {
        if (tag == 10) {
          DO_(::google::protobuf::internal::WireFormatLite::ReadString(
                input, this->mutable_name()));
        } else {
          goto handle_unusual;
        }
        if (input->ExpectTag(18)) goto parse_scClientDevId;
        break;
      }

      // optional string scClientDevId = 2;
      case 2: {
        if (tag == 18) {
         parse_scClientDevId:
          DO_(::google::protobuf::internal::WireFormatLite::ReadString(
                input, this->mutable_scclientdevid()));
        } else {
          goto handle_unusual;
        }
        if (input->ExpectTag(24)) goto parse_device_id;
        break;
      }

      // optional uint32 device_id = 3;
      case 3: {
        if (tag == 24) {
         parse_device_id:
          DO_((::google::protobuf::internal::WireFormatLite::ReadPrimitive<
                   ::google::protobuf::uint32, ::google::protobuf::internal::WireFormatLite::TYPE_UINT32>(
                 input, &device_id_)));
          set_has_device_id();
        } else {
          goto handle_unusual;
        }
        if (input->ExpectTag(34)) goto parse_supplement;
        break;
      }

      // optional bytes supplement = 4;
      case 4: {
        if (tag == 34) {
         parse_supplement:
          DO_(::google::protobuf::internal::WireFormatLite::ReadBytes(
                input, this->mutable_supplement()));
        } else {
          goto handle_unusual;
        }
        if (input->ExpectTag(42)) goto parse_message;
        break;
      }

      // optional bytes message = 5;
      case 5: {
        if (tag == 42) {
         parse_message:
          DO_(::google::protobuf::internal::WireFormatLite::ReadBytes(
                input, this->mutable_message()));
        } else {
          goto handle_unusual;
        }
        if (input->ExpectAtEnd()) goto success;
        break;
      }

      default: {
      handle_unusual:
        if (tag == 0 ||
            ::google::protobuf::internal::WireFormatLite::GetTagWireType(tag) ==
            ::google::protobuf::internal::WireFormatLite::WIRETYPE_END_GROUP) {
          goto success;
        }
        DO_(::google::protobuf::internal::WireFormatLite::SkipField(
            input, tag, &unknown_fields_stream));
        break;
      }
    }
  }
success:
  // @@protoc_insertion_point(parse_success:axolotl.MessageEnvelope)
  return true;
failure:
  // @@protoc_insertion_point(parse_failure:axolotl.MessageEnvelope)
  return false;
#undef DO_
}

void MessageEnvelope::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  // @@protoc_insertion_point(serialize_start:axolotl.MessageEnvelope)
  // optional string name = 1;
  if (has_name()) {
    ::google::protobuf::internal::WireFormatLite::WriteStringMaybeAliased(
      1, this->name(), output);
  }

  // optional string scClientDevId = 2;
  if (has_scclientdevid()) {
    ::google::protobuf::internal::WireFormatLite::WriteStringMaybeAliased(
      2, this->scclientdevid(), output);
  }

  // optional uint32 device_id = 3;
  if (has_device_id()) {
    ::google::protobuf::internal::WireFormatLite::WriteUInt32(3, this->device_id(), output);
  }

  // optional bytes supplement = 4;
  if (has_supplement()) {
    ::google::protobuf::internal::WireFormatLite::WriteBytesMaybeAliased(
      4, this->supplement(), output);
  }

  // optional bytes message = 5;
  if (has_message()) {
    ::google::protobuf::internal::WireFormatLite::WriteBytesMaybeAliased(
      5, this->message(), output);
  }

  output->WriteRaw(unknown_fields().data(),
                   unknown_fields().size());
  // @@protoc_insertion_point(serialize_end:axolotl.MessageEnvelope)
}

int MessageEnvelope::ByteSize() const {
  int total_size = 0;

  if (_has_bits_[0 / 32] & (0xffu << (0 % 32))) {
    // optional string name = 1;
    if (has_name()) {
      total_size += 1 +
        ::google::protobuf::internal::WireFormatLite::StringSize(
          this->name());
    }

    // optional string scClientDevId = 2;
    if (has_scclientdevid()) {
      total_size += 1 +
        ::google::protobuf::internal::WireFormatLite::StringSize(
          this->scclientdevid());
    }

    // optional uint32 device_id = 3;
    if (has_device_id()) {
      total_size += 1 +
        ::google::protobuf::internal::WireFormatLite::UInt32Size(
          this->device_id());
    }

    // optional bytes supplement = 4;
    if (has_supplement()) {
      total_size += 1 +
        ::google::protobuf::internal::WireFormatLite::BytesSize(
          this->supplement());
    }

    // optional bytes message = 5;
    if (has_message()) {
      total_size += 1 +
        ::google::protobuf::internal::WireFormatLite::BytesSize(
          this->message());
    }

  }
  total_size += unknown_fields().size();

  GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
  _cached_size_ = total_size;
  GOOGLE_SAFE_CONCURRENT_WRITES_END();
  return total_size;
}

void MessageEnvelope::CheckTypeAndMergeFrom(
    const ::google::protobuf::MessageLite& from) {
  MergeFrom(*::google::protobuf::down_cast<const MessageEnvelope*>(&from));
}

void MessageEnvelope::MergeFrom(const MessageEnvelope& from) {
  GOOGLE_CHECK_NE(&from, this);
  if (from._has_bits_[0 / 32] & (0xffu << (0 % 32))) {
    if (from.has_name()) {
      set_name(from.name());
    }
    if (from.has_scclientdevid()) {
      set_scclientdevid(from.scclientdevid());
    }
    if (from.has_device_id()) {
      set_device_id(from.device_id());
    }
    if (from.has_supplement()) {
      set_supplement(from.supplement());
    }
    if (from.has_message()) {
      set_message(from.message());
    }
  }
  mutable_unknown_fields()->append(from.unknown_fields());
}

void MessageEnvelope::CopyFrom(const MessageEnvelope& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool MessageEnvelope::IsInitialized() const {

  return true;
}

void MessageEnvelope::Swap(MessageEnvelope* other) {
  if (other != this) {
    std::swap(name_, other->name_);
    std::swap(scclientdevid_, other->scclientdevid_);
    std::swap(device_id_, other->device_id_);
    std::swap(supplement_, other->supplement_);
    std::swap(message_, other->message_);
    std::swap(_has_bits_[0], other->_has_bits_[0]);
    _unknown_fields_.swap(other->_unknown_fields_);
    std::swap(_cached_size_, other->_cached_size_);
  }
}

::std::string MessageEnvelope::GetTypeName() const {
  return "axolotl.MessageEnvelope";
}


// @@protoc_insertion_point(namespace_scope)

}  // namespace axolotl

// @@protoc_insertion_point(global_scope)
