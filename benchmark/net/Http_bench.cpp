#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Buffer.hpp"
#include "chaoxi/net/http/HttpContext.hpp"
#include "chaoxi/net/http/HttpResponse.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <benchmark/benchmark.h>

namespace
{

[[nodiscard]] std::string makeRequest(std::size_t headerCount)
{
    std::string request = "GET /api/v1/items?page=2 HTTP/1.1\r\n"
                          "Host: localhost\r\n";
    request.reserve(64 + headerCount * 24);
    for (std::size_t i = 0; i < headerCount; ++i)
    {
        request += "X-Benchmark-";
        request += std::to_string(i);
        request += ": value\r\n";
    }
    request += "Connection: keep-alive\r\n\r\n";
    return request;
}

static void BM_HttpContext_ParseSingle(benchmark::State& state)
{
    const auto headerCount = static_cast<std::size_t>(state.range(0));
    const auto request = makeRequest(headerCount);
    for (auto _ : state)
    {
        chaoxi::net::Buffer input;
        input.append(request);
        chaoxi::net::HttpContext context;

        const bool ok = context.parseRequest(input, chaoxi::Timestamp{});
        if (!ok || !context.gotAll())
        {
            state.SkipWithError("valid HTTP request failed to parse");
            break;
        }
        benchmark::DoNotOptimize(context);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(request.size()));
}

BENCHMARK(BM_HttpContext_ParseSingle)->Arg(0)->Arg(4)->Arg(16)->Arg(64);

static void BM_HttpContext_ParsePipeline(benchmark::State& state)
{
    const auto depth = static_cast<std::size_t>(state.range(0));
    const auto request = makeRequest(4);
    std::string pipeline;
    pipeline.reserve(request.size() * depth);
    for (std::size_t i = 0; i < depth; ++i)
    {
        pipeline += request;
    }

    for (auto _ : state)
    {
        chaoxi::net::Buffer input;
        input.append(pipeline);
        chaoxi::net::HttpContext context;
        for (std::size_t i = 0; i < depth; ++i)
        {
            const bool ok = context.parseRequest(input, chaoxi::Timestamp{});
            if (!ok || !context.gotAll())
            {
                state.SkipWithError("valid HTTP pipeline failed to parse");
                return;
            }
            benchmark::DoNotOptimize(context.request());
            context.reset();
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(depth));
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(pipeline.size()));
}

BENCHMARK(BM_HttpContext_ParsePipeline)->Arg(16);

static void BM_HttpContext_ParseByteFragmented(benchmark::State& state)
{
    const auto request = makeRequest(4);
    for (auto _ : state)
    {
        chaoxi::net::Buffer input;
        chaoxi::net::HttpContext context;
        for (char byte : request)
        {
            input.append(&byte, 1);
            if (!context.parseRequest(input, chaoxi::Timestamp{}))
            {
                state.SkipWithError("fragmented HTTP request failed to parse");
                return;
            }
        }
        if (!context.gotAll())
        {
            state.SkipWithError("fragmented HTTP request remained incomplete");
            return;
        }
        benchmark::DoNotOptimize(context.request());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(request.size()));
}

BENCHMARK(BM_HttpContext_ParseByteFragmented);

static void BM_HttpResponse_Serialize(benchmark::State& state)
{
    const auto bodySize = static_cast<std::size_t>(state.range(0));
    chaoxi::net::HttpResponse response(false);
    response.setStatusCode(chaoxi::net::HttpResponse::HttpStatusCode::k200Ok);
    response.setStatusMessage("OK");
    response.setContentType("application/json");
    response.addHeader("Server", "chaoxi");
    response.setBody(std::string(bodySize, 'x'));
    chaoxi::net::Buffer output;

    for (auto _ : state)
    {
        response.appendToBuffer(output);
        benchmark::DoNotOptimize(output.peek());
        output.retrieveAll();
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(bodySize));
}

BENCHMARK(BM_HttpResponse_Serialize)->Arg(0)->Arg(128)->Arg(4'096);

}  // namespace
