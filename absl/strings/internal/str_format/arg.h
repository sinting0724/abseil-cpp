#ifndef ABSL_STRINGS_INTERNAL_STR_FORMAT_ARG_H_
#define ABSL_STRINGS_INTERNAL_STR_FORMAT_ARG_H_

#include <string.h>
#include <wchar.h>

#include <cstdio>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>

#include "absl/base/port.h"
#include "absl/meta/type_traits.h"
#include "absl/numeric/int128.h"
#include "absl/strings/internal/str_format/extension.h"
#include "absl/strings/string_view.h"

class Cord;
class CordReader;

namespace absl {

class FormatCountCapture;
class FormatSink;

namespace str_format_internal {

template <typename T, typename = void>
struct HasUserDefinedConvert : std::false_type {};

template <typename T>
struct HasUserDefinedConvert<
    T, void_t<decltype(AbslFormatConvert(
           std::declval<const T&>(), std::declval<ConversionSpec>(),
           std::declval<FormatSink*>()))>> : std::true_type {};

template <typename T>
class StreamedWrapper;

// If 'v' can be converted (in the printf sense) according to 'conv',
// then convert it, appending to `sink` and return `true`.
// Otherwise fail and return `false`.

// Raw pointers.
struct VoidPtr {
  VoidPtr() = default;
  template <typename T,
            decltype(reinterpret_cast<uintptr_t>(std::declval<T*>())) = 0>
  VoidPtr(T* ptr)  // NOLINT
      : value(ptr ? reinterpret_cast<uintptr_t>(ptr) : 0) {}
  uintptr_t value;
};
ConvertResult<Conv::p> FormatConvertImpl(VoidPtr v, ConversionSpec conv,
                                         FormatSinkImpl* sink);

// Strings.
ConvertResult<Conv::s> FormatConvertImpl(const std::string& v,
                                         ConversionSpec conv,
                                         FormatSinkImpl* sink);
ConvertResult<Conv::s> FormatConvertImpl(string_view v, ConversionSpec conv,
                                         FormatSinkImpl* sink);
ConvertResult<Conv::s | Conv::p> FormatConvertImpl(const char* v,
                                                   ConversionSpec conv,
                                                   FormatSinkImpl* sink);
template <class AbslCord,
          typename std::enable_if<
              std::is_same<AbslCord, ::Cord>::value>::type* = nullptr,
          class AbslCordReader = ::CordReader>
ConvertResult<Conv::s> FormatConvertImpl(const AbslCord& value,
                                         ConversionSpec conv,
                                         FormatSinkImpl* sink) {
  if (conv.conv().id() != ConversionChar::s) return {false};

  bool is_left = conv.flags().left;
  size_t space_remaining = 0;

  int width = conv.width();
  if (width >= 0) space_remaining = width;

  size_t to_write = value.size();

  int precision = conv.precision();
  if (precision >= 0)
    to_write = (std::min)(to_write, static_cast<size_t>(precision));

  space_remaining = Excess(to_write, space_remaining);

  if (space_remaining > 0 && !is_left) sink->Append(space_remaining, ' ');

  string_view piece;
  for (AbslCordReader reader(value);
       to_write > 0 && reader.ReadFragment(&piece); to_write -= piece.size()) {
    if (piece.size() > to_write) piece.remove_suffix(piece.size() - to_write);
    sink->Append(piece);
  }

  if (space_remaining > 0 && is_left) sink->Append(space_remaining, ' ');
  return {true};
}

using IntegralConvertResult =
    ConvertResult<Conv::c | Conv::numeric | Conv::star>;
using FloatingConvertResult = ConvertResult<Conv::floating>;

// Floats.
FloatingConvertResult FormatConvertImpl(float v, ConversionSpec conv,
                                        FormatSinkImpl* sink);
FloatingConvertResult FormatConvertImpl(double v, ConversionSpec conv,
                                        FormatSinkImpl* sink);
FloatingConvertResult FormatConvertImpl(long double v, ConversionSpec conv,
                                        FormatSinkImpl* sink);

// Chars.
IntegralConvertResult FormatConvertImpl(char v, ConversionSpec conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(signed char v, ConversionSpec conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(unsigned char v, ConversionSpec conv,
                                        FormatSinkImpl* sink);

// Ints.
IntegralConvertResult FormatConvertImpl(short v,  // NOLINT
                                        ConversionSpec conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(unsigned short v,  // NOLINT
                                        ConversionSpec conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(int v, ConversionSpec conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(unsigned v, ConversionSpec conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(long v,  // NOLINT
                                        ConversionSpec conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(unsigned long v,  // NOLINT
                                        ConversionSpec conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(long long v,  // NOLINT
                                        ConversionSpec conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(unsigned long long v,  // NOLINT
                                        ConversionSpec conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(int128 v, ConversionSpec conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(uint128 v, ConversionSpec conv,
                                        FormatSinkImpl* sink);
template <typename T, enable_if_t<std::is_same<T, bool>::value, int> = 0>
IntegralConvertResult FormatConvertImpl(T v, ConversionSpec conv,
                                        FormatSinkImpl* sink) {
  return FormatConvertImpl(static_cast<int>(v), conv, sink);
}

// We provide this function to help the checker, but it is never defined.
// FormatArgImpl will use the underlying Convert functions instead.
template <typename T>
typename std::enable_if<std::is_enum<T>::value &&
                            !HasUserDefinedConvert<T>::value,
                        IntegralConvertResult>::type
FormatConvertImpl(T v, ConversionSpec conv, FormatSinkImpl* sink);

template <typename T>
ConvertResult<Conv::s> FormatConvertImpl(const StreamedWrapper<T>& v,
                                         ConversionSpec conv,
                                         FormatSinkImpl* out) {
  std::ostringstream oss;
  oss << v.v_;
  if (!oss) return {false};
  return str_format_internal::FormatConvertImpl(oss.str(), conv, out);
}

// Use templates and dependent types to delay evaluation of the function
// until after FormatCountCapture is fully defined.
struct FormatCountCaptureHelper {
  template <class T = int>
  static ConvertResult<Conv::n> ConvertHelper(const FormatCountCapture& v,
                                              ConversionSpec conv,
                                              FormatSinkImpl* sink) {
    const absl::enable_if_t<sizeof(T) != 0, FormatCountCapture>& v2 = v;

    if (conv.conv().id() != str_format_internal::ConversionChar::n)
      return {false};
    *v2.p_ = static_cast<int>(sink->size());
    return {true};
  }
};

template <class T = int>
ConvertResult<Conv::n> FormatConvertImpl(const FormatCountCapture& v,
                                         ConversionSpec conv,
                                         FormatSinkImpl* sink) {
  return FormatCountCaptureHelper::ConvertHelper(v, conv, sink);
}

// Helper friend struct to hide implementation details from the public API of
// FormatArgImpl.
struct FormatArgImplFriend {
  template <typename Arg>
  static bool ToInt(Arg arg, int* out) {
    // A value initialized ConversionSpec has a `none` conv, which tells the
    // dispatcher to run the `int` conversion.
    return arg.dispatcher_(arg.data_, {}, out);
  }

  template <typename Arg>
  static bool Convert(Arg arg, str_format_internal::ConversionSpec conv,
                      FormatSinkImpl* out) {
    return arg.dispatcher_(arg.data_, conv, out);
  }

  template <typename Arg>
  static typename Arg::Dispatcher GetVTablePtrForTest(Arg arg) {
    return arg.dispatcher_;
  }
};

// A type-erased handle to a format argument.
class FormatArgImpl {
 private:
  enum { kInlinedSpace = 8 };

  using VoidPtr = str_format_internal::VoidPtr;

  union Data {
    const void* ptr;
    const volatile void* volatile_ptr;
    char buf[kInlinedSpace];
  };

  using Dispatcher = bool (*)(Data, ConversionSpec, void* out);

  template <typename T>
  struct store_by_value
      : std::integral_constant<bool, (sizeof(T) <= kInlinedSpace) &&
                                         (std::is_integral<T>::value ||
                                          std::is_floating_point<T>::value ||
                                          std::is_pointer<T>::value ||
                                          std::is_same<VoidPtr, T>::value)> {};

  enum StoragePolicy { ByPointer, ByVolatilePointer, ByValue };
  template <typename T>
  struct storage_policy
      : std::integral_constant<StoragePolicy,
                               (std::is_volatile<T>::value
                                    ? ByVolatilePointer
                                    : (store_by_value<T>::value ? ByValue
                                                                : ByPointer))> {
  };

  // To reduce the number of vtables we will decay values before hand.
  // Anything with a user-defined Convert will get its own vtable.
  // For everything else:
  //   - Decay char* and char arrays into `const char*`
  //   - Decay any other pointer to `const void*`
  //   - Decay all enums to their underlying type.
  //   - Decay function pointers to void*.
  template <typename T, typename = void>
  struct DecayType {
    static constexpr bool kHasUserDefined =
        str_format_internal::HasUserDefinedConvert<T>::value;
    using type = typename std::conditional<
        !kHasUserDefined && std::is_convertible<T, const char*>::value,
        const char*,
        typename std::conditional<!kHasUserDefined &&
                                      std::is_convertible<T, VoidPtr>::value,
                                  VoidPtr, const T&>::type>::type;
  };
  template <typename T>
  struct DecayType<T,
                   typename std::enable_if<
                       !str_format_internal::HasUserDefinedConvert<T>::value &&
                       std::is_enum<T>::value>::type> {
    using type = typename std::underlying_type<T>::type;
  };

 public:
  template <typename T>
  explicit FormatArgImpl(const T& value) {
    using D = typename DecayType<T>::type;
    static_assert(
        std::is_same<D, const T&>::value || storage_policy<D>::value == ByValue,
        "Decayed types must be stored by value");
    Init(static_cast<D>(value));
  }

 private:
  friend struct str_format_internal::FormatArgImplFriend;
  template <typename T, StoragePolicy = storage_policy<T>::value>
  struct Manager;

  template <typename T>
  struct Manager<T, ByPointer> {
    static Data SetValue(const T& value) {
      Data data;
      data.ptr = std::addressof(value);
      return data;
    }

    static const T& Value(Data arg) { return *static_cast<const T*>(arg.ptr); }
  };

  template <typename T>
  struct Manager<T, ByVolatilePointer> {
    static Data SetValue(const T& value) {
      Data data;
      data.volatile_ptr = &value;
      return data;
    }

    static const T& Value(Data arg) {
      return *static_cast<const T*>(arg.volatile_ptr);
    }
  };

  template <typename T>
  struct Manager<T, ByValue> {
    static Data SetValue(const T& value) {
      Data data;
      memcpy(data.buf, &value, sizeof(value));
      return data;
    }

    static T Value(Data arg) {
      T value;
      memcpy(&value, arg.buf, sizeof(T));
      return value;
    }
  };

  template <typename T>
  void Init(const T& value) {
    data_ = Manager<T>::SetValue(value);
    dispatcher_ = &Dispatch<T>;
  }

  template <typename T>
  static int ToIntVal(const T& val) {
    using CommonType = typename std::conditional<std::is_signed<T>::value,
                                                 int64_t, uint64_t>::type;
    if (static_cast<CommonType>(val) >
        static_cast<CommonType>((std::numeric_limits<int>::max)())) {
      return (std::numeric_limits<int>::max)();
    } else if (std::is_signed<T>::value &&
               static_cast<CommonType>(val) <
                   static_cast<CommonType>((std::numeric_limits<int>::min)())) {
      return (std::numeric_limits<int>::min)();
    }
    return static_cast<int>(val);
  }

  template <typename T>
  static bool ToInt(Data arg, int* out, std::true_type /* is_integral */,
                    std::false_type) {
    *out = ToIntVal(Manager<T>::Value(arg));
    return true;
  }

  template <typename T>
  static bool ToInt(Data arg, int* out, std::false_type,
                    std::true_type /* is_enum */) {
    *out = ToIntVal(static_cast<typename std::underlying_type<T>::type>(
        Manager<T>::Value(arg)));
    return true;
  }

  template <typename T>
  static bool ToInt(Data, int*, std::false_type, std::false_type) {
    return false;
  }

  template <typename T>
  static bool Dispatch(Data arg, ConversionSpec spec, void* out) {
    // A `none` conv indicates that we want the `int` conversion.
    if (ABSL_PREDICT_FALSE(spec.conv().id() == ConversionChar::none)) {
      return ToInt<T>(arg, static_cast<int*>(out), std::is_integral<T>(),
                      std::is_enum<T>());
    }

    return str_format_internal::FormatConvertImpl(
               Manager<T>::Value(arg), spec, static_cast<FormatSinkImpl*>(out))
        .value;
  }

  Data data_;
  Dispatcher dispatcher_;
};

#define ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(T, E) \
  E template bool FormatArgImpl::Dispatch<T>(Data, ConversionSpec, void*)

#define ABSL_INTERNAL_FORMAT_DISPATCH_OVERLOADS_EXPAND_(...)                   \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(str_format_internal::VoidPtr,     \
                                             __VA_ARGS__);                     \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(bool, __VA_ARGS__);               \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(char, __VA_ARGS__);               \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(signed char, __VA_ARGS__);        \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(unsigned char, __VA_ARGS__);      \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(short, __VA_ARGS__); /* NOLINT */ \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(unsigned short,      /* NOLINT */ \
                                             __VA_ARGS__);                     \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(int, __VA_ARGS__);                \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(unsigned int, __VA_ARGS__);       \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(long, __VA_ARGS__); /* NOLINT */  \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(unsigned long,      /* NOLINT */  \
                                             __VA_ARGS__);                     \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(long long, /* NOLINT */           \
                                             __VA_ARGS__);                     \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(unsigned long long, /* NOLINT */  \
                                             __VA_ARGS__);                     \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(int128, __VA_ARGS__);             \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(uint128, __VA_ARGS__);            \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(float, __VA_ARGS__);              \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(double, __VA_ARGS__);             \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(long double, __VA_ARGS__);        \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(const char*, __VA_ARGS__);        \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(std::string, __VA_ARGS__);        \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(string_view, __VA_ARGS__)

ABSL_INTERNAL_FORMAT_DISPATCH_OVERLOADS_EXPAND_(extern);


}  // namespace str_format_internal
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_STR_FORMAT_ARG_H_
