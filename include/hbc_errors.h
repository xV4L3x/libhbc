//
// Created by Valerio Pio De Nicola on 17/04/26.
//

#pragma once

// -----------------------------------------------------------------------------
// hbc_errors.h
//
// Exception types thrown by libhbc.
//
// Design rules:
//   - All exceptions derive from std::exception so callers can always catch
//     a single base type if they don't care about the specific failure.
//   - Each class carries enough context to produce a useful error message
//     without requiring the caller to inspect internal state.
//   - No libhbc headers are included here — only the standard library.
//     This keeps hbc_errors.h safe to include anywhere, including in the
//     pybind11 bindings where we translate C++ exceptions into Python ones.
// -----------------------------------------------------------------------------

#include <stdexcept>  // std::runtime_error
#include <string>
#include <cstdint>    // uint32_t

namespace hbc {

// =============================================================================
// HBCError  (base class)
//
// The root of the libhbc exception hierarchy.
// Callers that don't need to distinguish specific failure kinds can catch this.
//
//   try { auto file = HBCLoader::load("app.hbc"); }
//   catch (const hbc::HBCError& e) { std::cerr << e.what(); }
// =============================================================================
class HBCError : public std::runtime_error {
public:
    explicit HBCError(const std::string& message)
        : std::runtime_error(message) {}
};


// =============================================================================
// FileError
//
// Thrown when the file cannot be opened or memory-mapped.
// This happens before any HBC-specific parsing begins.
//
// Examples:
//   - Path does not exist
//   - Process lacks read permission
//   - File is empty or smaller than the minimum header size
// =============================================================================
class FileError : public HBCError {
public:
    explicit FileError(const std::string& path, const std::string& reason)
        : HBCError("File error [" + path + "]: " + reason)
        , path_(path)
        , reason_(reason) {}

    // The path that was passed to HBCLoader::load()
    [[nodiscard]] const std::string& path()   const { return path_; }

    // The OS-level or size-check reason for the failure
    [[nodiscard]] const std::string& reason() const { return reason_; }

private:
    std::string path_;
    std::string reason_;
};


// =============================================================================
// InvalidMagicError
//
// Thrown when the first 8 bytes of the file do not match HBC_MAGIC.
// This means the file is not a Hermes bytecode file at all — it could be
// a plain JavaScript bundle, a ZIP/APK that wasn't extracted, or random data.
//
// `actual_magic` is included in the message so the analyst can see what the
// file actually starts with, which helps diagnose "forgot to unzip the APK".
// =============================================================================
class InvalidMagicError : public HBCError {
public:
    explicit InvalidMagicError(const std::string& path, uint64_t actual_magic)
        : HBCError(formatMessage(path, actual_magic))
        , path_(path)
        , actual_magic_(actual_magic) {}

    [[nodiscard]] const std::string& path()         const { return path_; }
    [[nodiscard]] uint64_t           actual_magic() const { return actual_magic_; }

private:
    std::string path_;
    uint64_t    actual_magic_;

    static std::string formatMessage(const std::string& path, uint64_t magic) {
        // Format the magic bytes as hex for the error message.
        // We do this inline to avoid pulling in <sstream> or <format>.
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%016llX",
                 static_cast<unsigned long long>(magic));
        return "Not a Hermes bytecode file [" + path + "]: "
               "expected magic 0x1F1903C103BC1FC6, got " + buf;
    }
};


// =============================================================================
// UnsupportedVersionError
//
// Thrown when the HBC version number in the header is not in our version
// registry — i.e. we have no opcode table for it.
//
// This is a normal, expected failure mode: new React Native releases ship
// new HBC versions. The fix is to add the corresponding version JSON file
// to libhbc/versions/. The error message tells the user exactly which
// version is missing so they know what to add.
// =============================================================================
class UnsupportedVersionError : public HBCError {
public:
    explicit UnsupportedVersionError(const std::string& path, uint32_t version)
        : HBCError("Unsupported HBC version [" + path + "]: "
                   "version " + std::to_string(version) +
                   " has no opcode table in libhbc/versions/. "
                   "Add v" + std::to_string(version) + ".json to add support.")
        , path_(path)
        , version_(version) {}

    [[nodiscard]] const std::string& path()    const { return path_; }
    [[nodiscard]] uint32_t           version() const { return version_; }

private:
    std::string path_;
    uint32_t    version_;
};


// =============================================================================
// ParseError
//
// Thrown when the file has a valid magic and a known version, but the binary
// data is corrupt, truncated, or internally inconsistent.
//
// `offset` is the byte position in the file where the problem was detected.
// This is the most important diagnostic field — it lets the analyst open the
// file in a hex editor and inspect exactly what went wrong.
// =============================================================================
class ParseError : public HBCError {
public:
    explicit ParseError(const std::string& context,
                        uint32_t           offset,
                        const std::string& reason)
        : HBCError(formatMessage(context, offset, reason))
        , context_(context)
        , offset_(offset)
        , reason_(reason) {}

    // Human-readable description of what was being parsed (e.g. "string table",
    // "function #42 instruction stream", "exception handler table")
    [[nodiscard]] const std::string& context() const { return context_; }

    // Byte offset in the file where the failure occurred
    [[nodiscard]] uint32_t           offset()  const { return offset_; }

    // The specific reason for the failure
    [[nodiscard]] const std::string& reason()  const { return reason_; }

private:
    std::string context_;
    uint32_t    offset_;
    std::string reason_;

    static std::string formatMessage(const std::string& context,
                                     uint32_t           offset,
                                     const std::string& reason) {
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%08X", offset);
        return "Parse error in " + context + " at offset " + buf + ": " + reason;
    }
};

} // namespace hbc