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

constexpr std::string_view kRequest = "GET /api/v1/items?page=2 HTTP/1.1\r\n"
                                      "Host: localhost\r\n"
                                      "User-Agent: chaoxi-benchmark\r\n"
                                      "Accept: application/json\r\n"
                                      "Connection: keep-alive\r\n"
                                      "\r\n";

static void BM_HttpContext_ParseRequest(benchmark::State& state)
{
    for (auto _ : state)
    {
        chaoxi::net::Buffer input;
        input.append(kRequest);
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
                            static_cast<std::int64_t>(kRequest.size()));
}

BENCHMARK(BM_HttpContext_ParseRequest);

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
