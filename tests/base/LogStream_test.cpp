#include "chaoxi/base/LogStream.hpp"

#include <limits>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

using std::string;

TEST(LogStreamTest, testLogStreamBooleans)
{
  chaoxi::LogStream os;
  const chaoxi::LogStream::Buffer& buf = os.buffer();
  EXPECT_EQ(buf.toString(), string(""));
  os << true;
  EXPECT_EQ(buf.toString(), string("1"));
  os << '\n';
  EXPECT_EQ(buf.toString(), string("1\n"));
  os << false;
  EXPECT_EQ(buf.toString(), string("1\n0"));
}

TEST(LogStreamTest, testLogStreamIntegers)
{
  chaoxi::LogStream os;
  const chaoxi::LogStream::Buffer& buf = os.buffer();
  EXPECT_EQ(buf.toString(), string(""));
  os << 1;
  EXPECT_EQ(buf.toString(), string("1"));
  os << 0;
  EXPECT_EQ(buf.toString(), string("10"));
  os << -1;
  EXPECT_EQ(buf.toString(), string("10-1"));
  os.resetBuffer();

  os << 0 << " " << 123 << 'x' << 0x64;
  EXPECT_EQ(buf.toString(), string("0 123x100"));
}

TEST(LogStreamTest, testLogStreamIntegerLimits)
{
  chaoxi::LogStream os;
  const chaoxi::LogStream::Buffer& buf = os.buffer();
  os << -2147483647;
  EXPECT_EQ(buf.toString(), string("-2147483647"));
  os << static_cast<int>(-2147483647 - 1);
  EXPECT_EQ(buf.toString(), string("-2147483647-2147483648"));
  os << ' ';
  os << 2147483647;
  EXPECT_EQ(buf.toString(), string("-2147483647-2147483648 2147483647"));
  os.resetBuffer();

  os << std::numeric_limits<int16_t>::min();
  EXPECT_EQ(buf.toString(), string("-32768"));
  os.resetBuffer();

  os << std::numeric_limits<int16_t>::max();
  EXPECT_EQ(buf.toString(), string("32767"));
  os.resetBuffer();

  os << std::numeric_limits<uint16_t>::min();
  EXPECT_EQ(buf.toString(), string("0"));
  os.resetBuffer();

  os << std::numeric_limits<uint16_t>::max();
  EXPECT_EQ(buf.toString(), string("65535"));
  os.resetBuffer();

  os << std::numeric_limits<int32_t>::min();
  EXPECT_EQ(buf.toString(), string("-2147483648"));
  os.resetBuffer();

  os << std::numeric_limits<int32_t>::max();
  EXPECT_EQ(buf.toString(), string("2147483647"));
  os.resetBuffer();

  os << std::numeric_limits<uint32_t>::min();
  EXPECT_EQ(buf.toString(), string("0"));
  os.resetBuffer();

  os << std::numeric_limits<uint32_t>::max();
  EXPECT_EQ(buf.toString(), string("4294967295"));
  os.resetBuffer();

  os << std::numeric_limits<int64_t>::min();
  EXPECT_EQ(buf.toString(), string("-9223372036854775808"));
  os.resetBuffer();

  os << std::numeric_limits<int64_t>::max();
  EXPECT_EQ(buf.toString(), string("9223372036854775807"));
  os.resetBuffer();

  os << std::numeric_limits<uint64_t>::min();
  EXPECT_EQ(buf.toString(), string("0"));
  os.resetBuffer();

  os << std::numeric_limits<uint64_t>::max();
  EXPECT_EQ(buf.toString(), string("18446744073709551615"));
  os.resetBuffer();

  int16_t a = 0;
  int32_t b = 0;
  int64_t c = 0;
  os << a;
  os << b;
  os << c;
  EXPECT_EQ(buf.toString(), string("000"));
}

TEST(LogStreamTest, testLogStreamFloats)
{
  chaoxi::LogStream os;
  const chaoxi::LogStream::Buffer& buf = os.buffer();

  os << 0.0;
  EXPECT_EQ(buf.toString(), string("0"));
  os.resetBuffer();

  os << 1.0;
  EXPECT_EQ(buf.toString(), string("1"));
  os.resetBuffer();

  os << 0.1;
  EXPECT_EQ(buf.toString(), string("0.1"));
  os.resetBuffer();

  os << 0.05;
  EXPECT_EQ(buf.toString(), string("0.05"));
  os.resetBuffer();

  os << 0.15;
  EXPECT_EQ(buf.toString(), string("0.15"));
  os.resetBuffer();

  double a = 0.1;
  os << a;
  EXPECT_EQ(buf.toString(), string("0.1"));
  os.resetBuffer();

  double b = 0.05;
  os << b;
  EXPECT_EQ(buf.toString(), string("0.05"));
  os.resetBuffer();

  double c = 0.15;
  os << c;
  EXPECT_EQ(buf.toString(), string("0.15"));
  os.resetBuffer();

  os << a+b;
  EXPECT_EQ(buf.toString(), string("0.15"));
  os.resetBuffer();

  EXPECT_TRUE(a+b != c);

  os << 1.23456789;
  EXPECT_EQ(buf.toString(), string("1.23456789"));
  os.resetBuffer();

  os << 1.234567;
  EXPECT_EQ(buf.toString(), string("1.234567"));
  os.resetBuffer();

  os << -123.456;
  EXPECT_EQ(buf.toString(), string("-123.456"));
  os.resetBuffer();
}

TEST(LogStreamTest, testLogStreamVoid)
{
  chaoxi::LogStream os;
  const chaoxi::LogStream::Buffer& buf = os.buffer();

  os << static_cast<void*>(0);
  EXPECT_EQ(buf.toString(), string("0x0"));
  os.resetBuffer();

  os << reinterpret_cast<void*>(8888);
  EXPECT_EQ(buf.toString(), string("0x22b8"));
  os.resetBuffer();
}

TEST(LogStreamTest, testLogStreamStrings)
{
  chaoxi::LogStream os;
  const chaoxi::LogStream::Buffer& buf = os.buffer();

  os << "Hello ";
  EXPECT_EQ(buf.toString(), string("Hello "));

  string chenshuo = "Shuo Chen";
  os << chenshuo;
  EXPECT_EQ(buf.toString(), string("Hello Shuo Chen"));
}

TEST(LogStreamTest, testLogStreamFmts)
{
  chaoxi::LogStream os;
  const chaoxi::LogStream::Buffer& buf = os.buffer();

  os.format("{:4d}", 1);
  EXPECT_EQ(buf.toString(), string("   1"));
  os.resetBuffer();

  os.format("{:4.2f}", 1.2);
  EXPECT_EQ(buf.toString(), string("1.20"));
  os.resetBuffer();

  os.format("{:4.2f}", 1.2);
  os.format("{:4d}", 43);
  EXPECT_EQ(buf.toString(), string("1.20  43"));
  os.resetBuffer();
}

TEST(LogStreamTest, testLogStreamLong)
{
  chaoxi::LogStream os;
  const chaoxi::LogStream::Buffer& buf = os.buffer();
  for (int i = 0; i < 399; ++i)
  {
    os << "123456789 ";
    EXPECT_EQ(buf.length(), 10*(i+1));
    EXPECT_EQ(buf.avail(), 4000 - 10*(i+1));
  }

  os << "abcdefghi ";
  EXPECT_EQ(buf.length(), 3990);
  EXPECT_EQ(buf.avail(), 10);

  os << "abcdefghi";
  EXPECT_EQ(buf.length(), 3999);
  EXPECT_EQ(buf.avail(), 1);
}

TEST(LogStreamTest, testFormatSI)
{
  EXPECT_EQ(chaoxi::formatSI(0), string("0"));
  EXPECT_EQ(chaoxi::formatSI(999), string("999"));
  EXPECT_EQ(chaoxi::formatSI(1000), string("1.00k"));
  EXPECT_EQ(chaoxi::formatSI(9990), string("9.99k"));
  EXPECT_EQ(chaoxi::formatSI(9994), string("9.99k"));
  EXPECT_EQ(chaoxi::formatSI(9995), string("10.0k"));
  EXPECT_EQ(chaoxi::formatSI(10000), string("10.0k"));
  EXPECT_EQ(chaoxi::formatSI(10049), string("10.0k"));
  EXPECT_EQ(chaoxi::formatSI(10050), string("10.1k"));
  EXPECT_EQ(chaoxi::formatSI(99900), string("99.9k"));
  EXPECT_EQ(chaoxi::formatSI(99949), string("99.9k"));
  EXPECT_EQ(chaoxi::formatSI(99950), string("100k"));
  EXPECT_EQ(chaoxi::formatSI(100499), string("100k"));
  EXPECT_EQ(chaoxi::formatSI(100501), string("101k"));
  EXPECT_EQ(chaoxi::formatSI(999499), string("999k"));
  EXPECT_EQ(chaoxi::formatSI(999500), string("1.00M"));
  EXPECT_EQ(chaoxi::formatSI(1004999), string("1.00M"));
  EXPECT_EQ(chaoxi::formatSI(1005001), string("1.01M"));
  EXPECT_EQ(chaoxi::formatSI(INT64_MAX), string("9.22E"));
}

TEST(LogStreamTest, testFormatIEC)
{
  EXPECT_EQ(chaoxi::formatIEC(0), string("0"));
  EXPECT_EQ(chaoxi::formatIEC(1023), string("1023"));
  EXPECT_EQ(chaoxi::formatIEC(1024), string("1.00Ki"));
  EXPECT_EQ(chaoxi::formatIEC(10234), string("9.99Ki"));
  EXPECT_EQ(chaoxi::formatIEC(10235), string("10.0Ki"));
  EXPECT_EQ(chaoxi::formatIEC(10240), string("10.0Ki"));
  EXPECT_EQ(chaoxi::formatIEC(10291), string("10.0Ki"));
  EXPECT_EQ(chaoxi::formatIEC(10292), string("10.1Ki"));
  EXPECT_EQ(chaoxi::formatIEC(102348), string("99.9Ki"));
  EXPECT_EQ(chaoxi::formatIEC(102349), string("100Ki"));
  EXPECT_EQ(chaoxi::formatIEC(102912), string("100Ki"));
  EXPECT_EQ(chaoxi::formatIEC(102913), string("101Ki"));
  EXPECT_EQ(chaoxi::formatIEC(1022976), string("999Ki"));
  EXPECT_EQ(chaoxi::formatIEC(1047552), string("1023Ki"));
  EXPECT_EQ(chaoxi::formatIEC(1047961), string("1023Ki"));
  EXPECT_EQ(chaoxi::formatIEC(1048063), string("1023Ki"));
  EXPECT_EQ(chaoxi::formatIEC(1048064), string("1.00Mi"));
  EXPECT_EQ(chaoxi::formatIEC(1048576), string("1.00Mi"));
  EXPECT_EQ(chaoxi::formatIEC(10480517), string("9.99Mi"));
  EXPECT_EQ(chaoxi::formatIEC(10480518), string("10.0Mi"));
  EXPECT_EQ(chaoxi::formatIEC(INT64_MAX), string("8.00Ei"));
}
