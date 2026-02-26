
#pragma once

#include <cstdint>
#include <ios>
#include <limits>
#include <string>
#include <type_traits>

constexpr size_t kMaxStringBytes = 4096;
constexpr uint32_t kMaxU16Length = 4096;

template <typename StreamT>
void MarkSerializationFailure(StreamT& stream) {
    stream.setstate(std::ios::failbit);
}

template <typename StreamT>
bool EnsureReadableBytes(StreamT& istream, std::streamsize bytes) {
    if (istream.fail() || istream.bad()) {
        return false;
    }

    if (istream.rdbuf()->in_avail() < bytes) {
        MarkSerializationFailure(istream);
        return false;
    }

    return true;
}

inline int SerializationByteSwapFlagIndex() {
    static const int index = std::ios_base::xalloc();
    return index;
}

template <typename StreamT>
void SetSerializationByteSwap(StreamT& stream, bool enabled) {
    stream.iword(SerializationByteSwapFlagIndex()) = enabled ? 1 : 0;
}

template <typename StreamT>
bool GetSerializationByteSwap(StreamT& stream) {
    return stream.iword(SerializationByteSwapFlagIndex()) != 0;
}

template <typename T>
constexpr T ByteSwapIntegral(T value) {
    using UnsignedT = typename std::make_unsigned<T>::type;
    auto input = static_cast<UnsignedT>(value);
    UnsignedT output = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        output = static_cast<UnsignedT>((output << 8) | (input & 0xFFu));
        input >>= 8;
    }

    return static_cast<T>(output);
}

// integral types

template <typename StreamT, typename T,
    typename std::enable_if_t<std::is_integral<T>::value, int> = 0>
void read(StreamT& istream, T& value) {
    if (!EnsureReadableBytes(istream, static_cast<std::streamsize>(sizeof(T)))) {
        return;
    }

    istream.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (istream.fail() || istream.bad()) {
        return;
    }

    if (GetSerializationByteSwap(istream)) {
        value = ByteSwapIntegral(value);
    }
}

template <typename StreamT, typename T,
    typename std::enable_if_t<std::is_integral<T>::value, int> = 0>
void write(StreamT& ostream, const T& value) {
    T serializedValue = value;
    if (GetSerializationByteSwap(ostream)) {
        serializedValue = ByteSwapIntegral(serializedValue);
    }

    ostream.write(reinterpret_cast<const char*>(&serializedValue), sizeof(T));
}

// enumeration types with integral underlying types

template <typename StreamT, typename T,
    typename std::enable_if_t<std::is_enum<T>::value, int> = 0>
void read(StreamT& istream, T& value) {
    using RawT = typename std::underlying_type<T>::type;
    RawT raw;
    read(istream, raw);
    value = static_cast<T>(raw);
}

template <typename StreamT, typename T,
    typename std::enable_if_t<std::is_enum<T>::value, int> = 0>
void write(StreamT& ostream, const T& value) {
    using RawT = typename std::underlying_type<T>::type;
    write(ostream, static_cast<RawT>(value));
}

// boolean types

template <typename StreamT>
void read(StreamT& istream, bool& value) {
    uint8_t boolAsInt;
    read(istream, boolAsInt);
    value = (boolAsInt != 0);
}

template <typename StreamT>
void write(StreamT& ostream, const bool& value) {
    uint8_t boolAsInt = value ? 1 : 0;
    write(ostream, boolAsInt);
}

// std::string types

template <typename StreamT>
void read(StreamT& istream, std::string& value) {
    uint16_t length;
    read(istream, length);
    if (istream.fail() || istream.bad()) {
        return;
    }

    if (length > kMaxStringBytes) {
        MarkSerializationFailure(istream);
        return;
    }

    if (!EnsureReadableBytes(istream, static_cast<std::streamsize>(length))) {
        return;
    }

    value.resize(length);
    if (length == 0) {
        return;
    }

    istream.read(&value[0], length);
}

template <typename StreamT>
void write(StreamT& ostream, const std::string& value) {
    if (value.length() > std::numeric_limits<uint16_t>::max() || value.length() > kMaxStringBytes) {
        MarkSerializationFailure(ostream);
        return;
    }

    uint16_t length = static_cast<uint16_t>(value.length());
    write(ostream, length);
    if (ostream.fail() || ostream.bad() || length == 0) {
        return;
    }

    ostream.write(value.data(), length);
}

// std::u16string types

template <typename StreamT>
void read(StreamT& istream, std::u16string& value) {
    uint32_t length;
    read(istream, length);
    if (istream.fail() || istream.bad()) {
        return;
    }

    if (length > kMaxU16Length) {
        MarkSerializationFailure(istream);
        return;
    }

    const auto requiredBytes = static_cast<std::streamsize>(length) * static_cast<std::streamsize>(sizeof(uint16_t));
    if (!EnsureReadableBytes(istream, requiredBytes)) {
        return;
    }

    value.resize(length);
    uint16_t tmp;
    for (uint32_t i = 0; i < length; ++i) {
        read(istream, tmp);
        if (istream.fail() || istream.bad()) {
            return;
        }

        value[i] = tmp;
    }
}

template <typename StreamT>
void write(StreamT& ostream, const std::u16string& value) {
    if (value.length() > std::numeric_limits<uint32_t>::max() || value.length() > kMaxU16Length) {
        MarkSerializationFailure(ostream);
        return;
    }

    uint32_t length = static_cast<uint32_t>(value.length());
    write(ostream, length);
    if (ostream.fail() || ostream.bad()) {
        return;
    }

    uint16_t tmp;
    for (uint32_t i = 0; i < length; ++i) {
        tmp = static_cast<uint16_t>(value[i]);
        write(ostream, tmp);
        if (ostream.fail() || ostream.bad()) {
            return;
        }
    }
}

// Specialized Read Types

template <typename T, typename StreamT>
T read(StreamT& istream) {
    T tmp;
    read(istream, tmp);
    return tmp;
}

template <typename T, typename StreamT>
T readAt(StreamT& istream, size_t offset) {
    istream.seekg(offset);
    return read<T>(istream);
}

// Similar to readAt, but preserves the read position of the stream
template <typename T, typename StreamT>
T peekAt(StreamT& istream, size_t offset) {
    auto pos = istream.tellg();
    T val = readAt<T>(istream, offset);
    istream.seekg(pos);
    return val;
}
