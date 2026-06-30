#include <Arduino.h>
#include <unity.h>

struct ParsedPayload
{
    String pid;
    String key;
};

// Mirrors the parsing currently used inside taskSQL_HTTP.
static ParsedPayload parseLikeTaskSqlHttp(const String &results)
{
    ParsedPayload parsed;
    int index = results.indexOf("|");
    parsed.pid = results.substring(0, index);
    int index1 = results.indexOf(",");
    parsed.key = results.substring(index + 1, index1);
    return parsed;
}

void test_matching_pid_and_key_should_not_trigger_delete_logic()
{
    ParsedPayload parsed = parseLikeTaskSqlHttp("15|15,ok");
    TEST_ASSERT_EQUAL_STRING("15", parsed.pid.c_str());
    TEST_ASSERT_EQUAL_STRING("15", parsed.key.c_str());
    TEST_ASSERT_TRUE(parsed.pid == parsed.key);
}

void test_mismatching_pid_and_key_should_trigger_delete_logic()
{
    ParsedPayload parsed = parseLikeTaskSqlHttp("22|18,ok");
    TEST_ASSERT_EQUAL_STRING("22", parsed.pid.c_str());
    TEST_ASSERT_EQUAL_STRING("18", parsed.key.c_str());
    TEST_ASSERT_TRUE(parsed.pid != parsed.key);
}

void test_first_comma_is_used_for_key_parsing()
{
    ParsedPayload parsed = parseLikeTaskSqlHttp("9|9,ok,extra");
    TEST_ASSERT_EQUAL_STRING("9", parsed.pid.c_str());
    TEST_ASSERT_EQUAL_STRING("9", parsed.key.c_str());
}

void test_string_comparison_is_exact()
{
    ParsedPayload parsed = parseLikeTaskSqlHttp("105|0105,ok");
    TEST_ASSERT_EQUAL_STRING("105", parsed.pid.c_str());
    TEST_ASSERT_EQUAL_STRING("0105", parsed.key.c_str());
    TEST_ASSERT_TRUE(parsed.pid != parsed.key);
}

void setup()
{
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_matching_pid_and_key_should_not_trigger_delete_logic);
    RUN_TEST(test_mismatching_pid_and_key_should_trigger_delete_logic);
    RUN_TEST(test_first_comma_is_used_for_key_parsing);
    RUN_TEST(test_string_comparison_is_exact);
    UNITY_END();
}

void loop()
{
}
