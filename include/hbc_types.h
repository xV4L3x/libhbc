//
// Created by Valerio Pio De Nicola on 17/04/26.
//

#pragma once

// -----------------------------------------------------------------------------
// hbc_types.h
//
// Pure data types for the Hermes Bytecode (HBC) intermediate representation.
//
// RULES for this file:
//   - No functions that contain logic (constructors with defaults are fine)
//   - No includes beyond the C++ standard library
//   - No dependencies on any other libhbc header
//
// Every other file in libhbc depends on this one. Nothing here depends on
// anything else. This is intentional and must be preserved.
// -----------------------------------------------------------------------------

#include <cstdint>   // uint8_t, uint16_t, uint32_t, uint64_t
#include <string>    // std::string
#include <vector>    // std::vector
#include <optional>  // std::optional
#include <unordered_map> // std::unordered_map

namespace hbc {
    // =============================================================================
    // CONSTANTS
    // =============================================================================

    // The first 8 bytes of every valid .hbc file.
    // Encodes the Unicode string 'Ἑρμῆ' (Hermēs in Greek) as UTF-16BE.
    constexpr uint64_t HBC_MAGIC = 0x1F1903C103BC1FC6ULL;


    // =============================================================================
    // ENUMERATIONS
    // =============================================================================

    // Classifies each entry in the string table.
    // The Hermes VM uses this to distinguish interned identifiers from regular
    // string literals and from strings predefined by the runtime.
    enum class StringKind : uint8_t {
        String      = 0,  // Regular string literal (e.g. "hello world")
        Identifier  = 1,  // Identifier used as a property key (e.g. "length")
        Predefined  = 2,  // Predefined by the Hermes runtime (e.g. "undefined")
    };

    // Classifies the kind of value an instruction operand carries.
    // This is our own classification — not part of the binary format.
    // The parser assigns these based on the opcode definition for each operand slot.
    enum class OperandKind : uint8_t {
        Reg8,           // 8-bit register index  (e.g. r0, r1 ... r255)
        Reg32,          // 32-bit register index (rare, large functions)
        UInt8,          // Unsigned 8-bit literal
        UInt16,         // Unsigned 16-bit literal
        UInt32,         // Unsigned 32-bit literal
        Imm32,          // Signed 32-bit immediate (e.g. integer constants)
        Double,         // 64-bit IEEE 754 double
        StringID,       // Index into the string table → resolves to a string
        FunctionID,     // Index into the function table → resolves to a function
        BuiltinID,      // Index into the Hermes built-in function table
        JumpTarget,     // Byte offset of a jump, resolved to instruction index
        EnvSlot,        // Index into the current environment (closure variable)
    };

    // Classifies how a function was defined in the original JavaScript source.
    // Hermes records this in the function header flags.
    enum class FunctionKind : uint8_t {
        Normal      = 0,
        Generator   = 1,
        Async       = 2,
        AsyncGenerator = 3,
    };


    // =============================================================================
    // HBCHeader
    //
    // Mirrors the binary layout of the .hbc file header.
    // Field names and types are taken directly from BytecodeFileFormat.h
    // in the Hermes source tree.
    //
    // Important: some fields are version-dependent (e.g. BigIntCount was added
    // in v87). The parser is responsible for populating optional fields correctly
    // based on the detected version. Fields that do not exist in an older version
    // are left at their zero-initialised default.
    // =============================================================================
    struct HBCHeader {
        uint64_t magic               = 0;  // Must equal HBC_MAGIC
        uint32_t version             = 0;  // HBC format version (e.g. 84, 96)
        uint8_t  source_hash[20]     = {}; // SHA-1 of the original JS source
        uint32_t file_length         = 0;  // Total byte length of the .hbc file
        uint32_t global_code_index   = 0;  // Index of the top-level function
        uint32_t function_count      = 0;
        uint32_t string_kind_count   = 0;
        uint32_t identifier_count    = 0;
        uint32_t string_count        = 0;
        uint32_t overflow_string_count = 0;
        uint32_t string_storage_size = 0;

        // Fields added in v87 — zero if version < 87
        uint32_t big_int_count       = 0;
        uint32_t big_int_storage_size = 0;

        uint32_t reg_exp_count       = 0;
        uint32_t reg_exp_storage_size = 0;
        uint32_t array_buffer_size   = 0;
        uint32_t obj_key_buffer_size = 0;
        uint32_t obj_value_buffer_size = 0;
        uint32_t segment_id          = 0;
        uint32_t cjs_module_count    = 0;
        uint32_t function_source_count = 0;
        uint32_t debug_info_offset   = 0;

        // Packed option flags from the header.
        // We unpack these into named booleans for clarity.
        bool     static_builtins     = false;
        bool     cjs_modules_statically_resolved = false;
        bool     has_async           = false;
    };

    // =============================================================================
    // StringEntry
    //
    // One resolved entry from the HBC string table.
    // The parser resolves every string at load time so callers never need to
    // deal with raw offsets or UTF-16 decoding.
    // =============================================================================
    struct StringEntry {
        uint32_t    id           = 0;     // Index in the string table (stable key)
        std::string value;                // Decoded UTF-8 string value
        StringKind  kind         = StringKind::String;
        bool        is_utf16     = false; // True if the source encoding was UTF-16

        // These are preserved from the binary so the serializer can reconstruct
        // the table without re-encoding strings that were not modified.
        uint32_t    raw_offset   = 0;     // Byte offset in the string storage section
        uint32_t    raw_length   = 0;     // Byte length in the string storage section
    };


    // =============================================================================
    // StringTable
    //
    // Holds all string entries plus two O(1) lookup indexes built at parse time.
    // Callers should always use the indexes rather than scanning `entries`.
    // =============================================================================
    struct StringTable {
        std::vector<StringEntry> entries;

        // id → decoded string value.  Built by the indexer after parsing.
        std::unordered_map<uint32_t, std::string> by_id;

        // decoded string value → id.  Useful for the AI module ("find all
        // functions that reference the string 'authToken'") and for the patcher
        // when injecting new string references.
        std::unordered_map<std::string, uint32_t> by_value;
    };


    // =============================================================================
    // Operand
    //
    // One operand of a decoded HBC instruction.
    //
    // `raw_value` always holds the numeric value exactly as it appears in the
    // binary — this is what the serializer writes back.  The `resolved_*` fields
    // are populated by the parser for operand types that reference other parts of
    // the file, so callers get meaningful data without manual lookups.
    // =============================================================================
    struct Operand {
        OperandKind kind       = OperandKind::UInt8;
        uint64_t    raw_value  = 0;  // Always valid; used by the serializer

        // Populated when kind == StringID
        std::string resolved_string;

        // Populated when kind == FunctionID
        uint32_t    resolved_func_id  = 0;

        // Populated when kind == JumpTarget.
        // Stored as a signed instruction index relative to the start of the
        // function (0-based).  Negative values mean "jump backwards".
        int32_t     resolved_jump_index = 0;
    };


    // =============================================================================
    // HBCInstruction
    //
    // One fully-decoded instruction inside a function body.
    //
    // `index` is the instruction's position within the function (0 = first).
    // `byte_offset` is its position in bytes from the start of the function's
    // bytecode — needed by the serializer and patcher.
    //
    // All boolean classification flags are pre-computed by the indexer so the
    // CFG builder in the Python layer never re-examines opcode names.
    // =============================================================================
    struct HBCInstruction {
        uint32_t    index        = 0;  // Position within the function (0-based)
        uint32_t    byte_offset  = 0;  // Byte offset from start of function body

        uint8_t     opcode       = 0;  // Raw opcode byte (for serialization)
        std::string opcode_name;       // Human-readable name e.g. "LoadConstString"

        std::vector<Operand> operands;

        // Pre-computed classification flags (set by the indexer).
        // These are the only fields the CFG builder needs to read.
        bool is_jump             = false;  // Any jump (conditional or not)
        bool is_conditional_jump = false;  // Jmp that depends on a register value
        bool is_call             = false;  // Any call instruction
        bool is_return           = false;  // Ret or RetLong
        bool is_throw            = false;  // Throw instruction

        // True when this instruction ends a basic block.
        // Equivalent to: is_jump || is_return || is_throw
        // Computed once by the indexer; used heavily by the CFG builder.
        bool terminates_block    = false;
    };


    // =============================================================================
    // ExceptionHandler
    //
    // One entry from a function's exception handler table.
    // The `start`, `end`, and `target` fields are stored as instruction indexes
    // (not byte offsets) after the parser resolves them.
    // =============================================================================
    struct ExceptionHandler {
        uint32_t start  = 0;  // First instruction index covered by this handler
        uint32_t end    = 0;  // Last instruction index covered (exclusive)
        uint32_t target = 0;  // Instruction index of the catch/finally block
        uint32_t depth  = 0;  // Nesting depth (0 = outermost)
    };


    // =============================================================================
    // DebugInfo
    //
    // Optional source location data.  Only present if the .hbc file was compiled
    // with debug info enabled (most production bundles omit this).
    // =============================================================================
    struct DebugInfo {
        uint32_t    line     = 0;
        uint32_t    column   = 0;
        std::string filename;  // Resolved from the debug string table
    };


    // =============================================================================
    // HBCFunction
    //
    // One compiled JavaScript function.  This is the primary unit of analysis —
    // the CFG builder, decompiler, and AI module all operate on individual
    // HBCFunction objects.
    //
    // `byte_offset` and `byte_length` are preserved from the binary so the
    // patcher can locate and overwrite function bodies precisely.
    // =============================================================================
    struct HBCFunction {
        // Identity
        uint32_t    id          = 0;    // Index in the function table (stable key)
        uint32_t    name_id     = 0;    // String table index of the function name
        std::string name;               // Resolved at parse time (may be empty for
        // anonymous functions → "<anonymous>")

        // Calling convention metadata
        uint32_t    param_count          = 0;
        uint32_t    frame_size           = 0;  // Number of registers used
        uint32_t    environment_size     = 0;  // Number of captured variables
        uint8_t     highest_read_cache_index  = 0;
        uint8_t     highest_write_cache_index = 0;

        // Flags
        FunctionKind kind                = FunctionKind::Normal;
        bool         is_strict_mode      = false;
        bool         prohibits_dynamic_scoping = false;
        bool         has_exception_handler    = false;
        bool         has_debug_info           = false;

        // The instruction stream — the entire decoded body of the function
        std::vector<HBCInstruction> instructions;

        // Exception handlers (empty if has_exception_handler == false)
        std::vector<ExceptionHandler> exception_handlers;

        // Source location (empty optional if debug info is absent)
        std::optional<DebugInfo> debug_info;

        // Binary layout — needed by the serializer and patcher
        uint32_t    byte_offset = 0;  // Offset of this function's bytecode in file
        uint32_t    byte_length = 0;  // Byte length of this function's bytecode
    };


    // =============================================================================
    // HBCFile
    //
    // The top-level IR object.  Returned by HBCLoader::load() and accepted by
    // HBCLoader::save().  Everything downstream operates on this struct.
    //
    // The two unordered_maps at the bottom are O(1) lookup indexes built by the
    // indexer after parsing.  On an 80 MB file these replace what would otherwise
    // be O(n) linear scans on every query.
    // =============================================================================
    struct HBCFile {
        HBCHeader   header;
        StringTable string_table;

        // Raw buffer sections — preserved verbatim for the serializer.
        // Analysis code should not read these directly; use the string_table
        // and the per-function resolved operands instead.
        std::vector<uint8_t> array_buffer;
        std::vector<uint8_t> obj_key_buffer;
        std::vector<uint8_t> obj_value_buffer;

        // All functions, in the order they appear in the file.
        // functions[header.global_code_index] is the top-level entry point.
        std::vector<HBCFunction> functions;

        // -------------------------------------------------------------------------
        // Indexes — built once by the indexer, queried O(1) thereafter
        // -------------------------------------------------------------------------

        // function id → pointer into functions vector.
        // Using a raw pointer here is safe because HBCFile owns the vector and
        // pointers remain stable once the vector is fully populated.
        std::unordered_map<uint32_t, HBCFunction*> function_by_id;

        // byte offset in file → function id.
        // Used by the patcher to locate which function contains a given address.
        std::unordered_map<uint32_t, uint32_t> function_by_offset;
    };

}
