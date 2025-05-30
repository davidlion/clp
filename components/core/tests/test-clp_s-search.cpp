#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "../src/clp_s/ArchiveReader.hpp"
#include "../src/clp_s/CommandLineArguments.hpp"
#include "../src/clp_s/InputConfig.hpp"
#include "../src/clp_s/search/ast/ConvertToExists.hpp"
#include "../src/clp_s/search/ast/EmptyExpr.hpp"
#include "../src/clp_s/search/ast/Expression.hpp"
#include "../src/clp_s/search/ast/NarrowTypes.hpp"
#include "../src/clp_s/search/ast/OrOfAndForm.hpp"
#include "../src/clp_s/search/EvaluateTimestampIndex.hpp"
#include "../src/clp_s/search/kql/kql.hpp"
#include "../src/clp_s/search/Output.hpp"
#include "../src/clp_s/search/OutputHandler.hpp"
#include "../src/clp_s/search/Projection.hpp"
#include "../src/clp_s/search/SchemaMatch.hpp"
#include "../src/clp_s/Utils.hpp"
#include "clp_s_test_utils.hpp"
#include "TestOutputCleaner.hpp"

constexpr std::string_view cTestSearchArchiveDirectory{"test-clp-s-search-archive"};
constexpr std::string_view cTestInputFileDirectory{"test_log_files"};
constexpr std::string_view cTestSearchInputFile{"test_search.jsonl"};
constexpr std::string_view cTestIdxKey{"idx"};

namespace {
auto get_test_input_path_relative_to_tests_dir() -> std::filesystem::path;
auto get_test_input_local_path() -> std::string;
void
search(std::string const& query, bool ignore_case, std::vector<int64_t> const& expected_results);
void validate_results(
        std::vector<clp_s::search::VectorOutputHandler::QueryResult> const& results,
        std::vector<int64_t> const& expected_results
);

auto get_test_input_path_relative_to_tests_dir() -> std::filesystem::path {
    return std::filesystem::path{cTestInputFileDirectory} / cTestSearchInputFile;
}

auto get_test_input_local_path() -> std::string {
    std::filesystem::path const current_file_path{__FILE__};
    auto const tests_dir{current_file_path.parent_path()};
    return (tests_dir / get_test_input_path_relative_to_tests_dir()).string();
}

void validate_results(
        std::vector<clp_s::search::VectorOutputHandler::QueryResult> const& results,
        std::vector<int64_t> const& expected_results
) {
    std::set<int64_t> results_set;
    bool results_are_valid_json = true;
    for (auto const& result : results) {
        try {
            auto json = nlohmann::json::parse(result.message);
            results_set.insert(json[cTestIdxKey].template get<int64_t>());
        } catch (std::exception const& e) {
            FAIL(fmt::format("Invalid JSON in result: {}", result.message));
            return;
        }
    }
    std::set<int64_t> expected_results_set{expected_results.begin(), expected_results.end()};
    REQUIRE(results_set == expected_results_set);
    REQUIRE(results.size() == expected_results.size());
}

void
search(std::string const& query, bool ignore_case, std::vector<int64_t> const& expected_results) {
    REQUIRE(expected_results.size() > 0);
    auto query_stream = std::istringstream{query};
    auto expr = clp_s::search::kql::parse_kql_expression(query_stream);
    REQUIRE(nullptr != expr);
    REQUIRE(nullptr == std::dynamic_pointer_cast<clp_s::search::ast::EmptyExpr>(expr));

    clp_s::search::ast::OrOfAndForm standardize_pass;
    expr = standardize_pass.run(expr);
    REQUIRE(nullptr != expr);

    clp_s::search::ast::NarrowTypes narrow_pass;
    expr = narrow_pass.run(expr);
    REQUIRE(nullptr != expr);

    clp_s::search::ast::ConvertToExists convert_pass;
    expr = convert_pass.run(expr);
    REQUIRE(nullptr != expr);

    std::vector<clp_s::search::VectorOutputHandler::QueryResult> results;
    for (auto const& entry : std::filesystem::directory_iterator(cTestSearchArchiveDirectory)) {
        auto archive_reader = std::make_shared<clp_s::ArchiveReader>();
        auto archive_path = clp_s::Path{
                .source{clp_s::InputSource::Filesystem},
                .path{entry.path().string()}
        };
        archive_reader->open(archive_path, clp_s::NetworkAuthOption{});

        auto archive_expr = expr->copy();

        auto timestamp_dict = archive_reader->get_timestamp_dictionary();
        clp_s::search::EvaluateTimestampIndex timestamp_index_pass(timestamp_dict);
        REQUIRE(clp_s::EvaluatedValue::False != timestamp_index_pass.run(archive_expr));

        auto match_pass = std::make_shared<clp_s::search::SchemaMatch>(
                archive_reader->get_schema_tree(),
                archive_reader->get_schema_map()
        );
        archive_expr = match_pass->run(archive_expr);
        REQUIRE(nullptr != archive_expr);

        auto output_handler = std::make_unique<clp_s::search::VectorOutputHandler>(results);
        clp_s::search::Output output_pass(
                match_pass,
                archive_expr,
                archive_reader,
                std::move(output_handler),
                ignore_case
        );
        output_pass.filter();
        archive_reader->close();
    }

    validate_results(results, expected_results);
}
}  // namespace

TEST_CASE("clp-s-search", "[clp-s][search]") {
    std::vector<std::pair<std::string, std::vector<int64_t>>> queries_and_results{
            {R"aa(NOT a: b)aa", {0}},
            {R"aa(msg: "Msg 1: \"Abc123\"")aa", {1}},
            {R"aa(msg: "Msg 2: 'Abc123'")aa", {2}},
            {R"aa(msg: "Msg 3: \nAbc123")aa", {3}},
            // CLP incorrectly generates no subqueries in Grep::process_raw_query for the following
            // query, so we skip it for now.
            //{R"aa(msg: "Msg 4: \\Abc123")aa", {4}}
            {R"aa(msg: "Msg 5: \rAbc123")aa", {5}},
            {R"aa(msg: "Msg 6: \tAbc123")aa", {6}},
            {R"aa(msg: "*Abc123*")aa", {1, 2, 3, 5, 6}},
            {R"aa(arr.b > 1000)aa", {7, 8}},
            {R"aa(var_string: *)aa", {9}},
            {R"aa(clp_string: *)aa", {9}}
    };
    auto structurize_arrays = GENERATE(true, false);
    auto single_file_archive = GENERATE(true, false);

    TestOutputCleaner const test_cleanup{{std::string{cTestSearchArchiveDirectory}}};

    REQUIRE_NOTHROW(compress_archive(
            get_test_input_local_path(),
            std::string{cTestSearchArchiveDirectory},
            single_file_archive,
            structurize_arrays,
            clp_s::CommandLineArguments::FileType::Json
    ));

    for (auto const& [query, expected_results] : queries_and_results) {
        REQUIRE_NOTHROW(search(query, false, expected_results));
    }
}
