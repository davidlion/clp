#include "DecomposedQuery.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <log_surgeon/log_surgeon.hpp>
#include <ystdlib/error_handling/Result.hpp>

#include <clpp/ErrorCode.hpp>
#include <clpp/TextShape.hpp>

namespace clpp {
namespace {
/**
 * Use the log-surgeon sub-query segments to build a query interpretation.
 */
auto build_interpretation(std::vector<log_surgeon::SubQuery> const& sub_queries)
        -> DecomposedQuery::Interpretation;

auto build_interpretation(std::vector<log_surgeon::SubQuery> const& sub_queries)
        -> DecomposedQuery::Interpretation {
    TextShape<std::string> shape_query;
    std::vector<DecomposedQuery::LeafQuery> leaf_queries;
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
}  // namespace

auto DecomposedQuery::decompose_by_rule_name(
        log_surgeon::Parser& parser,
        std::string_view query,
        std::string_view rule_name
) -> ystdlib::error_handling::Result<DecomposedQuery> {
    auto const interpretations{parser.search_by_name(query, rule_name)};
    if (interpretations.empty()) {
        return clpp::ClppErrorCode{clpp::ClppErrorCodeEnum::DecomposeQueryFailure};
    }

    std::vector<Interpretation> interps;
    interps.reserve(interpretations.size());
    for (auto const& sub_queries : interpretations) {
        interps.emplace_back(build_interpretation(sub_queries));
    }
    return DecomposedQuery{std::move(interps)};
}

auto DecomposedQuery::decompose_by_log_shapes(
        log_surgeon::Parser& parser,
        std::string_view query,
        std::span<std::string_view const> log_shapes
) -> std::vector<DecomposedQuery> {
    std::vector<log_surgeon::CCharArray> ffi_shapes;
    ffi_shapes.reserve(log_shapes.size());
    for (auto const shape : log_shapes) {
        ffi_shapes.push_back(log_surgeon::CCharArray::from_string_view(shape));
    }

    auto const interpretations_by_shapes{parser.search_by_log_shapes(query, ffi_shapes)};
    std::vector<DecomposedQuery> decomposed_queries;
    decomposed_queries.reserve(interpretations_by_shapes.size());
    for (auto const& interpretations : interpretations_by_shapes) {
        std::vector<Interpretation> interps;
        interps.reserve(interpretations.size());
        for (auto const& sub_queries : interpretations) {
            interps.emplace_back(build_interpretation(sub_queries));
        }
        decomposed_queries.push_back(DecomposedQuery{std::move(interps)});
    }
    return decomposed_queries;
}

auto DecomposedQuery::split_qualified_name(std::string_view const qualified_name)
        -> std::vector<std::string_view> {
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
