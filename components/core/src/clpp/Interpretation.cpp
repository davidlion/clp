#include "Interpretation.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if CLP_BUILD_CLPP_DECOMPOSITION
    #include <system_error>
#endif

#include <log_surgeon/log_surgeon.hpp>
#include <ystdlib/error_handling/Result.hpp>

#include <clpp/ErrorCode.hpp>
#include <clpp/TextShape.hpp>

namespace clpp {
namespace {
#if CLP_BUILD_CLPP_DECOMPOSITION
/**
 * Builds one interpretation from a log-surgeon sub-query segmentation: the segments without a rule
 * name form the shape query's static text, and each segment with a rule name becomes a leaf query
 * plus a placeholder in the shape query.
 */
auto build_interpretation(std::vector<log_surgeon::SubQuery> const& sub_queries) -> Interpretation;

auto build_interpretation(std::vector<log_surgeon::SubQuery> const& sub_queries) -> Interpretation {
    TextShape<std::string> shape_query;
    std::vector<LeafQuery> leaf_queries;
    for (auto const& sub_query : sub_queries) {
        if (sub_query.qualified_name.empty()) {
            shape_query.escape_and_append(sub_query.value);
        } else {
            leaf_queries.emplace_back(sub_query.qualified_name, sub_query.value);
            shape_query.append_placeholder(sub_query.qualified_name);
        }
    }
    return {std::move(shape_query), std::move(leaf_queries)};
}
#else
constexpr std::string_view cDecompositionUnsupportedMessage{
        "clp+ query decomposition is not supported in this build; rebuild with"
        " -DCLP_BUILD_CLPP_DECOMPOSITION=ON"
};
#endif
}  // namespace

#if CLP_BUILD_CLPP_DECOMPOSITION
auto decompose_by_rule_name(
        log_surgeon::Parser& parser,
        std::string_view query,
        std::string_view rule_name
) -> ystdlib::error_handling::Result<std::vector<Interpretation>> {
    auto const sub_query_sets{parser.search_by_name(query, rule_name)};
    if (sub_query_sets.empty()) {
        return clpp::ClppErrorCode{clpp::ClppErrorCodeEnum::DecomposeQueryFailure};
    }

    std::vector<Interpretation> interpretations;
    interpretations.reserve(sub_query_sets.size());
    for (auto const& sub_queries : sub_query_sets) {
        interpretations.emplace_back(build_interpretation(sub_queries));
    }
    return interpretations;
}

auto decompose_by_log_shapes(
        log_surgeon::Parser& parser,
        std::string_view query,
        std::span<std::string_view const> log_shapes
) -> std::vector<std::vector<Interpretation>> {
    std::vector<log_surgeon::CCharArray> ffi_shapes;
    ffi_shapes.reserve(log_shapes.size());
    for (auto const shape : log_shapes) {
        ffi_shapes.push_back(log_surgeon::CCharArray::from_string_view(shape));
    }

    auto const sub_query_sets{parser.search_by_log_shapes(query, ffi_shapes)};
    std::vector<std::vector<Interpretation>> interpretations_by_shape;
    interpretations_by_shape.reserve(sub_query_sets.size());
    for (auto const& sub_queries : sub_query_sets) {
        std::vector<Interpretation> interpretations;
        interpretations.reserve(sub_queries.size());
        for (auto const& sub_query_set : sub_queries) {
            interpretations.emplace_back(build_interpretation(sub_query_set));
        }
        interpretations_by_shape.push_back(std::move(interpretations));
    }
    return interpretations_by_shape;
}
#else
auto decompose_by_rule_name(log_surgeon::Parser&, std::string_view, std::string_view)
        -> ystdlib::error_handling::Result<std::vector<Interpretation>> {
    throw std::system_error{
            ystdlib::error_handling::make_error_code(
                    clpp::ClppErrorCode{clpp::ClppErrorCodeEnum::Unsupported}
            ),
            std::string{cDecompositionUnsupportedMessage}
    };
}

auto
decompose_by_log_shapes(log_surgeon::Parser&, std::string_view, std::span<std::string_view const>)
        -> std::vector<std::vector<Interpretation>> {
    throw std::system_error{
            ystdlib::error_handling::make_error_code(
                    clpp::ClppErrorCode{clpp::ClppErrorCodeEnum::Unsupported}
            ),
            std::string{cDecompositionUnsupportedMessage}
    };
}
#endif

auto split_qualified_name(std::string_view const qualified_name) -> std::vector<std::string_view> {
    std::vector<std::string_view> rule_names;
    size_t start{0};
    while (true) {
        auto end{qualified_name.find('.', start)};
        if (std::string::npos == end) {
            rule_names.emplace_back(qualified_name.substr(start));
            break;
        }
        rule_names.emplace_back(qualified_name.substr(start, end - start));
        start = end + 1;
    }
    return rule_names;
}
}  // namespace clpp
