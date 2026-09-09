#ifndef CLP_S_SEARCH_CLPPMATCHER_HPP
#define CLP_S_SEARCH_CLPPMATCHER_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <log_surgeon/log_surgeon.hpp>
#include <ystdlib/error_handling/Result.hpp>

#include <clp_s/ArchiveReader.hpp>
#include <clpp/Defs.hpp>
#include <clpp/Interpretation.hpp>

namespace clp_s::search {
/**
 * Utility for running clpp queries.
 */
class ClppMatcher {
public:
    // Types
    /**
     * An interpretation of a clpp query and the schema IDs containing a matching shape (log shape
     * or parent rule shape).
     */
    struct InterpretationMatch {
        std::unordered_set<int32_t> schema_ids;
        clpp::Interpretation interpretation;
    };

    // Constructors
    /**
     * Builds the log shape to schemas index using `archive_reader` to enable clpp querying.
     * @param archive_reader
     * @param case_sensitive Whether matching is case sensitive.
     * @throws std::runtime_error if the log shape dictionary is not valid.
     */
    ClppMatcher(std::shared_ptr<ArchiveReader> archive_reader, bool case_sensitive);

    // Methods
    /**
     * Finds the schemas whose log shapes (or parent rule shapes of `rule_name`) satisfy
     * `shape_query`.
     * @param rule_name A parent rule name, or empty to query the entire log shape.
     * @param shape_query A wildcard pattern, or std::nullopt to match every shape.
     * @return The matching schema IDs.
     */
    [[nodiscard]] auto find_matching_schemas(
            std::string_view rule_name,
            std::optional<std::string_view> shape_query
    ) const -> std::unordered_set<int32_t>;

    /**
     * Decomposes `query` against the parent rule `rule_name`, or against every log shape if
     * `rule_name` is empty, then returns the possible interpretations.
     * @param query
     * @param rule_name A qualified parent rule name, or empty to decompose against every log shape.
     * @return The matching interpretations, or an error code indicating the failure:
     * - Forwards `decompose_by_log_shape`'s return values.
     * - Forwards `decompose_by_rule_name`'s return values.
     */
    [[nodiscard]] auto decompose_query(std::string_view query, std::string_view rule_name)
            -> ystdlib::error_handling::Result<std::vector<InterpretationMatch>>;

private:
    // Methods
    /**
     * Decomposes `query` against the log shapes returning interpretations that matched a log shape.
     * @return The matching interpretations, or an error code indicating the failure:
     * - Forwards `ArchiveReader::read_parsing_spec`'s return values.
     */
    [[nodiscard]] auto decompose_by_log_shape(std::string_view query)
            -> ystdlib::error_handling::Result<std::vector<InterpretationMatch>>;

    /**
     * Decomposes `query` against the parent rule `rule_name`, then matches each interpretation's
     * shape query against `rule_name` in relevant log shapes.
     * @return The matching interpretations, or an error code indicating the failure:
     * - Forwards `clpp::decompose_by_rule_name`'s return values.
     * - Forwards `ArchiveReader::read_parsing_spec`'s return values.
     */
    [[nodiscard]] auto decompose_by_rule_name(std::string_view query, std::string_view rule_name)
            -> ystdlib::error_handling::Result<std::vector<InterpretationMatch>>;

    /**
     * Get the parent rule shapes named `rule_name` from `log_shape_id` or the entire log shape if
     * `rule_name` is empty.
     * @throw Propagates `ArchiveReader::get_parent_rule_shapes`'s exceptions.
     */
    [[nodiscard]] auto
    get_shapes(clpp::log_shape_id_t log_shape_id, std::string_view rule_name) const
            -> std::vector<std::string_view>;

    // Data members
    std::shared_ptr<ArchiveReader> m_archive_reader;
    bool m_case_sensitive{false};
    std::vector<std::unordered_set<int32_t>> m_schemas_by_log_shape;
    std::unique_ptr<log_surgeon::Parser> m_parser;
};
}  // namespace clp_s::search

#endif  // CLP_S_SEARCH_CLPPMATCHER_HPP
