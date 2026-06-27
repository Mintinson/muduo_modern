///
/// @file tests/net/http/Http_test.cpp
/// @brief HTTP 模块单元测试 — HttpRequest / HttpContext / HttpResponse
///

#include "chaoxi/net/Buffer.hpp"
#include "chaoxi/net/http/HttpContext.hpp"
#include "chaoxi/net/http/HttpRequest.hpp"
#include "chaoxi/net/http/HttpResponse.hpp"

#include <string>
#include <string_view>

#include <gtest/gtest.h>

using namespace chaoxi;
using namespace chaoxi::net;

// ============================================================================
// HttpRequest — method parsing
// ============================================================================

TEST(HttpRequestTest, SetMethodGet)
{
    HttpRequest req;
    EXPECT_TRUE(req.setMethod("GET"));
    EXPECT_EQ(req.method(), HttpRequest::Method::kGet);
    EXPECT_EQ(req.methodString(), "GET");
}

TEST(HttpRequestTest, SetMethodPost)
{
    HttpRequest req;
    EXPECT_TRUE(req.setMethod("POST"));
    EXPECT_EQ(req.method(), HttpRequest::Method::kPost);
    EXPECT_EQ(req.methodString(), "POST");
}

TEST(HttpRequestTest, SetMethodHead)
{
    HttpRequest req;
    EXPECT_TRUE(req.setMethod("HEAD"));
    EXPECT_EQ(req.method(), HttpRequest::Method::kHead);
}

TEST(HttpRequestTest, SetMethodPut)
{
    HttpRequest req;
    EXPECT_TRUE(req.setMethod("PUT"));
    EXPECT_EQ(req.method(), HttpRequest::Method::kPut);
}

TEST(HttpRequestTest, SetMethodDelete)
{
    HttpRequest req;
    EXPECT_TRUE(req.setMethod("DELETE"));
    EXPECT_EQ(req.method(), HttpRequest::Method::kDelete);
}

TEST(HttpRequestTest, SetMethodInvalid)
{
    HttpRequest req;
    EXPECT_FALSE(req.setMethod("PATCH"));
    EXPECT_EQ(req.method(), HttpRequest::Method::kInvalid);
    EXPECT_FALSE(req.setMethod(""));
    EXPECT_EQ(req.method(), HttpRequest::Method::kInvalid);
}

TEST(HttpRequestTest, SetMethodCaseSensitive)
{
    HttpRequest req;
    // HTTP method is case-sensitive per spec (though most servers accept both)
    EXPECT_FALSE(req.setMethod("get"));
    EXPECT_FALSE(req.setMethod("post"));
}

TEST(HttpRequestTest, MethodStringDefault)
{
    HttpRequest req;
    EXPECT_EQ(req.methodString(), "UNKNOWN");
}

// ============================================================================
// HttpRequest — path / query / version
// ============================================================================

TEST(HttpRequestTest, PathAndQuery)
{
    HttpRequest req;
    req.setPath("/index.html");
    EXPECT_EQ(req.path(), "/index.html");

    req.setQuery("key=value");
    EXPECT_EQ(req.query(), "key=value");
}

TEST(HttpRequestTest, Version)
{
    HttpRequest req;
    EXPECT_EQ(req.getVersion(), HttpRequest::Version::kUnknown);

    req.setVersion(HttpRequest::Version::kHttp11);
    EXPECT_EQ(req.getVersion(), HttpRequest::Version::kHttp11);

    req.setVersion(HttpRequest::Version::kHttp10);
    EXPECT_EQ(req.getVersion(), HttpRequest::Version::kHttp10);
}

TEST(HttpRequestTest, ReceiveTime)
{
    HttpRequest req;
    auto now = Timestamp::clock::now();
    req.setReceiveTime(now);
    EXPECT_EQ(req.receiveTime(), now);
}

// ============================================================================
// HttpRequest — headers
// ============================================================================

TEST(HttpRequestTest, AddAndGetHeader)
{
    HttpRequest req;
    req.addHeader("Host", "www.example.com");
    req.addHeader("User-Agent", "test/1.0");

    EXPECT_EQ(req.getHeader("Host"), "www.example.com");
    EXPECT_EQ(req.getHeader("User-Agent"), "test/1.0");
    EXPECT_EQ(req.getHeader("Accept"), "");  // not set → empty
}

TEST(HttpRequestTest, AddHeaderOverwrite)
{
    HttpRequest req;
    req.addHeader("Host", "a.com");
    req.addHeader("Host", "b.com");
    // insert_or_assign: last value wins
    EXPECT_EQ(req.getHeader("Host"), "b.com");
}

TEST(HttpRequestTest, AddHeaderPointer)
{
    HttpRequest req;
    std::string line = "Host: www.example.com";
    const char* colon = line.data() + 4;  // points to ':'
    req.addHeader(line.data(), colon, line.data() + line.size());
    EXPECT_EQ(req.getHeader("Host"), "www.example.com");
}

TEST(HttpRequestTest, AddHeaderPointerWithSpaces)
{
    HttpRequest req;
    std::string line = "Host:  www.example.com  ";
    const char* colon = line.data() + 4;
    req.addHeader(line.data(), colon, line.data() + line.size());
    EXPECT_EQ(req.getHeader("Host"), "www.example.com");
}

TEST(HttpRequestTest, HeadersView)
{
    HttpRequest req;
    req.addHeader("Content-Type", "text/html");
    req.addHeader("Server", "muduo");
    EXPECT_EQ(req.headers().size(), 2u);
}

// ============================================================================
// HttpContext — request line parsing
// ============================================================================

TEST(HttpContextTest, ParseRequestAllInOne)
{
    HttpContext context;
    Buffer input;
    input.append("GET /index.html HTTP/1.1\r\n"
                 "Host: www.chenshuo.com\r\n"
                 "\r\n");

    EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
    EXPECT_TRUE(context.gotAll());

    const auto& req = context.request();
    EXPECT_EQ(req.method(), HttpRequest::Method::kGet);
    EXPECT_EQ(req.path(), "/index.html");
    EXPECT_EQ(req.getVersion(), HttpRequest::Version::kHttp11);
    EXPECT_EQ(req.getHeader("Host"), "www.chenshuo.com");
    EXPECT_EQ(req.getHeader("User-Agent"), "");
}

TEST(HttpContextTest, ParseRequestInTwoPieces)
{
    std::string all("GET /index.html HTTP/1.1\r\n"
                    "Host: www.chenshuo.com\r\n"
                    "\r\n");

    for (size_t sz1 = 0; sz1 < all.size(); ++sz1)
    {
        HttpContext context;
        Buffer input;
        input.append(all.data(), sz1);
        EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
        EXPECT_FALSE(context.gotAll());

        input.append(all.data() + sz1, all.size() - sz1);
        EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
        EXPECT_TRUE(context.gotAll());

        const auto& req = context.request();
        EXPECT_EQ(req.method(), HttpRequest::Method::kGet);
        EXPECT_EQ(req.path(), "/index.html");
        EXPECT_EQ(req.getVersion(), HttpRequest::Version::kHttp11);
        EXPECT_EQ(req.getHeader("Host"), "www.chenshuo.com");
    }
}

TEST(HttpContextTest, ParseRequestWithQuery)
{
    HttpContext context;
    Buffer input;
    input.append("GET /search?q=hello&page=1 HTTP/1.1\r\n"
                 "Host: example.com\r\n"
                 "\r\n");

    EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
    EXPECT_TRUE(context.gotAll());

    const auto& req = context.request();
    EXPECT_EQ(req.path(), "/search");
    EXPECT_EQ(req.query(), "q=hello&page=1");
}

TEST(HttpContextTest, ParseRequestPost)
{
    HttpContext context;
    Buffer input;
    input.append("POST /api/data HTTP/1.1\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: 10\r\n"
                 "\r\n");

    EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
    EXPECT_TRUE(context.gotAll());

    const auto& req = context.request();
    EXPECT_EQ(req.method(), HttpRequest::Method::kPost);
    EXPECT_EQ(req.path(), "/api/data");
    EXPECT_EQ(req.getHeader("Content-Type"), "application/json");
    EXPECT_EQ(req.getHeader("Content-Length"), "10");
}

TEST(HttpContextTest, ParseRequestEmptyHeaderValue)
{
    HttpContext context;
    Buffer input;
    input.append("GET /index.html HTTP/1.1\r\n"
                 "Host: www.chenshuo.com\r\n"
                 "User-Agent:\r\n"
                 "Accept-Encoding: \r\n"
                 "\r\n");

    EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
    EXPECT_TRUE(context.gotAll());

    const auto& req = context.request();
    EXPECT_EQ(req.getHeader("User-Agent"), "");
    EXPECT_EQ(req.getHeader("Accept-Encoding"), "");
}

TEST(HttpContextTest, ParseRequestMultipleHeaders)
{
    HttpContext context;
    Buffer input;
    input.append("HEAD /status HTTP/1.0\r\n"
                 "Host: localhost\r\n"
                 "Accept: text/html\r\n"
                 "Accept-Language: en-US\r\n"
                 "\r\n");

    EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
    EXPECT_TRUE(context.gotAll());

    const auto& req = context.request();
    EXPECT_EQ(req.method(), HttpRequest::Method::kHead);
    EXPECT_EQ(req.getVersion(), HttpRequest::Version::kHttp10);
    EXPECT_EQ(req.path(), "/status");
    EXPECT_EQ(req.getHeader("Accept"), "text/html");
    EXPECT_EQ(req.getHeader("Accept-Language"), "en-US");
}

TEST(HttpContextTest, ParseRequestInvalidMethod)
{
    HttpContext context;
    Buffer input;
    input.append("PATCH /test HTTP/1.1\r\n\r\n");
    EXPECT_FALSE(context.parseRequest(input, Timestamp::clock::now()));
}

TEST(HttpContextTest, ParseRequestInvalidVersion)
{
    HttpContext context;
    Buffer input;
    input.append("GET /test HTTP/0.9\r\n\r\n");
    EXPECT_FALSE(context.parseRequest(input, Timestamp::clock::now()));
}

TEST(HttpContextTest, ParseRequestMalformedLine)
{
    HttpContext context;
    Buffer input;
    input.append("GARBAGE\r\n\r\n");
    EXPECT_FALSE(context.parseRequest(input, Timestamp::clock::now()));
}

TEST(HttpContextTest, ParseRequestIncompleteData)
{
    HttpContext context;
    Buffer input;
    input.append("GET /test HT");
    // Incomplete — should return true (no error yet) but gotAll is false
    EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
    EXPECT_FALSE(context.gotAll());
}

TEST(HttpContextTest, Reset)
{
    HttpContext context;
    Buffer input;
    input.append("GET /first HTTP/1.1\r\nHost: a\r\n\r\n");
    EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
    EXPECT_TRUE(context.gotAll());
    EXPECT_EQ(context.request().path(), "/first");

    context.reset();
    EXPECT_FALSE(context.gotAll());
    EXPECT_EQ(context.request().path(), "");

    input.append("GET /second HTTP/1.1\r\nHost: b\r\n\r\n");
    EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
    EXPECT_EQ(context.request().path(), "/second");
}

// ============================================================================
// HttpResponse — response generation
// ============================================================================

TEST(HttpResponseTest, StatusLine)
{
    HttpResponse resp(false);
    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp.setStatusMessage("OK");

    Buffer buf;
    resp.appendToBuffer(buf);

    std::string output = buf.retrieveAllAsString();
    EXPECT_NE(output.find("HTTP/1.1 200 OK"), std::string::npos);
}

TEST(HttpResponseTest, CloseConnection)
{
    HttpResponse resp(true);
    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp.setStatusMessage("OK");

    Buffer buf;
    resp.appendToBuffer(buf);

    std::string output = buf.retrieveAllAsString();
    EXPECT_NE(output.find("Connection: close"), std::string::npos);
}

TEST(HttpResponseTest, KeepAliveAndContentLength)
{
    HttpResponse resp(false);  // closeConnection = false → Keep-Alive
    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp.setStatusMessage("OK");
    resp.setBody("Hello World");

    Buffer buf;
    resp.appendToBuffer(buf);

    std::string output = buf.retrieveAllAsString();
    EXPECT_NE(output.find("Content-Length: 11"), std::string::npos);
    EXPECT_NE(output.find("Connection: Keep-Alive"), std::string::npos);
    EXPECT_NE(output.find("Hello World"), std::string::npos);
}

TEST(HttpResponseTest, CustomHeaders)
{
    HttpResponse resp(false);
    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp.setStatusMessage("OK");
    resp.addHeader("Server", "muduo_test");
    resp.addHeader("X-Frame-Options", "DENY");
    resp.setBody("<html></html>");

    Buffer buf;
    resp.appendToBuffer(buf);

    std::string output = buf.retrieveAllAsString();
    EXPECT_NE(output.find("Server: muduo_test"), std::string::npos);
    EXPECT_NE(output.find("X-Frame-Options: DENY"), std::string::npos);
}

TEST(HttpResponseTest, Http10Version)
{
    HttpResponse resp(false);
    resp.setVersion(HttpResponse::HttpVersion::kHttp10);
    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp.setStatusMessage("OK");

    Buffer buf;
    resp.appendToBuffer(buf);

    std::string output = buf.retrieveAllAsString();
    EXPECT_NE(output.find("HTTP/1.0"), std::string::npos);
}

TEST(HttpResponseTest, StatusCodes)
{
    struct Case
    {
        HttpResponse::HttpStatusCode code;
        std::string expected;
    };

    std::vector<Case> cases = {
        {HttpResponse::HttpStatusCode::k200Ok,               "200"},
        {HttpResponse::HttpStatusCode::k301MovedPermanently, "301"},
        {HttpResponse::HttpStatusCode::k400BadRequest,       "400"},
        {HttpResponse::HttpStatusCode::k404NotFound,         "404"},
    };

    for (auto& c : cases)
    {
        HttpResponse resp(false);
        resp.setStatusCode(c.code);
        resp.setStatusMessage("X");

        Buffer buf;
        resp.appendToBuffer(buf);
        std::string output = buf.retrieveAllAsString();
        EXPECT_NE(output.find(c.expected), std::string::npos)
            << "Failed for status " << c.expected;
    }
}

TEST(HttpResponseTest, ContentType)
{
    HttpResponse resp(false);
    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp.setStatusMessage("OK");
    resp.setContentType("text/html");
    resp.setBody("<h1>Hello</h1>");

    Buffer buf;
    resp.appendToBuffer(buf);

    std::string output = buf.retrieveAllAsString();
    EXPECT_NE(output.find("Content-Type: text/html"), std::string::npos);
}

TEST(HttpResponseTest, AddHeaderOverwrite)
{
    HttpResponse resp(false);
    resp.addHeader("X-Custom", "first");
    resp.addHeader("X-Custom", "second");  // should overwrite

    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp.setStatusMessage("OK");

    Buffer buf;
    resp.appendToBuffer(buf);

    std::string output = buf.retrieveAllAsString();
    // "second" should appear, "first" should not appear twice
    EXPECT_NE(output.find("X-Custom: second"), std::string::npos);
    EXPECT_EQ(output.find("first"), std::string::npos);
}

// ============================================================================
// HttpContext — edge cases
// ============================================================================

TEST(HttpContextTest, ParsePutDelete)
{
    for (auto method : {"PUT", "DELETE"})
    {
        HttpContext ctx;
        Buffer input;
        input.append(std::string(method) + " /resource HTTP/1.1\r\n\r\n");
        EXPECT_TRUE(ctx.parseRequest(input, Timestamp::clock::now()));
        EXPECT_TRUE(ctx.gotAll());
        if (method == std::string_view("PUT"))
        {
            EXPECT_EQ(ctx.request().method(), HttpRequest::Method::kPut);
        }
        else
        {
            EXPECT_EQ(ctx.request().method(), HttpRequest::Method::kDelete);
        }
    }
}

TEST(HttpContextTest, ParseRequestRootPath)
{
    HttpContext context;
    Buffer input;
    input.append("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
    EXPECT_EQ(context.request().path(), "/");
    EXPECT_EQ(context.request().query(), "");
}

TEST(HttpContextTest, ParseRequestPathWithQuestionNoQuery)
{
    // Trailing '?' with no query string
    HttpContext context;
    Buffer input;
    input.append("GET /path? HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(context.parseRequest(input, Timestamp::clock::now()));
    EXPECT_EQ(context.request().path(), "/path");
    EXPECT_EQ(context.request().query(), "");
}
