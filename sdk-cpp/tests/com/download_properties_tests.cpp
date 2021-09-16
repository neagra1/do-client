#include "tests_common.h"

#include <chrono>
#include <functional>
#include <thread>

#include <boost/filesystem.hpp>

#include "do_download.h"
#include "do_exceptions.h"
#include "test_data.h"
#include "test_helpers.h"

namespace msdo = microsoft::deliveryoptimization;
using namespace std::chrono_literals;

static double TimeOperation(const std::function<void()>& op)
{
    auto start = std::chrono::steady_clock::now();
    op();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

class DownloadPropertyTestsDOSVC : public ::testing::Test
{
public:
    void SetUp() override
    {
        TestHelpers::CleanTestDir();
        ASSERT_FALSE(boost::filesystem::exists(g_tmpFileName));
    }
    void TearDown() override
    {
        TestHelpers::CleanTestDir();
    }

    // Our build/test machines run Windows Server 2019, which use an older COM interface and does not support setting IntegrityCheckInfo through DODownloadProperty com interface
    // Accept multiple error codes to handle running tests both locally and on the build machine
    static void VerifyError(int32_t code, const std::vector<int32_t>& expectedErrors)
    {
        ASSERT_TRUE(std::find(expectedErrors.begin(), expectedErrors.end(), code) != expectedErrors.end());
    }

    static void VerifyCallWithExpectedErrors(const std::function<void()>& op, const std::vector<int32_t>& expectedErrors)
    {
        try
        {
            op();
        }
        catch( const msdo::exception& e)
        {
            VerifyError(e.error_code(), expectedErrors);
        }
    }
};

//TODO: Not sure how much value these tests are, functional tests utilize parsing log lines to verify these properties were set, could be useful here
//At the moment, these tests are essentially just verifying that these properties can be set and download succeeds
TEST_F(DownloadPropertyTestsDOSVC, SmallDownloadSetCallerNameTest)
{
    msdo::download simpleDownload(g_smallFileUrl, g_tmpFileName);

    msdo::download_property_value callerName(std::string("dosdkcpp_tests"));
    simpleDownload.set_property(msdo::download_property::caller_name, callerName);

    simpleDownload.start_and_wait_until_completion();
    ASSERT_TRUE(boost::filesystem::exists(g_tmpFileName));
}

TEST_F(DownloadPropertyTestsDOSVC, SmallDownloadWithPhfDigestandCvTest)
{
    msdo::download simpleDownload(g_smallFileUrl, g_tmpFileName);

    std::vector<int32_t> expectedErrors = { 0, static_cast<int32_t>(msdo::errc::do_e_unknown_property_id) };

    msdo::download_property_value integrityCheckMandatory(true);
    VerifyCallWithExpectedErrors([&]()
        {
            simpleDownload.set_property(msdo::download_property::integrity_check_mandatory, integrityCheckMandatory);
        }, expectedErrors);

    msdo::download_property_value integrityCheckInfo(g_smallFilePhfInfoJson);
    VerifyCallWithExpectedErrors([&]()
        {
            simpleDownload.set_property(msdo::download_property::integrity_check_info, integrityCheckInfo);
        }, expectedErrors);

    msdo::download_property_value correlationVector(std::string("g+Vo71JZwkmJdYfF.0"));
    VerifyCallWithExpectedErrors([&]()
        {
            simpleDownload.set_property(msdo::download_property::correlation_vector, correlationVector);
        }, expectedErrors);

    simpleDownload.start_and_wait_until_completion();

    ASSERT_TRUE(boost::filesystem::exists(g_tmpFileName));
}

TEST_F(DownloadPropertyTestsDOSVC, SmallDownloadWithPhfDigestandCvTestNoThrow)
{
    msdo::download simpleDownload(g_smallFileUrl, g_tmpFileName);

    std::vector<int32_t> expectedErrors = { 0, static_cast<int32_t>(msdo::errc::do_e_unknown_property_id) };

    msdo::download_property_value integrityCheckMandatory(true);
    int32_t code = simpleDownload.set_property_nothrow(msdo::download_property::integrity_check_mandatory, integrityCheckMandatory);
    VerifyError(code, expectedErrors);

    msdo::download_property_value integrityCheckInfo(g_smallFilePhfInfoJson);
    code = simpleDownload.set_property_nothrow(msdo::download_property::integrity_check_info, integrityCheckInfo);
    VerifyError(code, expectedErrors);

    msdo::download_property_value correlationVector(std::string("g+Vo71JZwkmJdYfF.0"));
    code = simpleDownload.set_property_nothrow(msdo::download_property::correlation_vector, correlationVector);
    VerifyError(code, expectedErrors);

    simpleDownload.start_and_wait_until_completion();

    ASSERT_TRUE(boost::filesystem::exists(g_tmpFileName));
}

TEST_F(DownloadPropertyTestsDOSVC, InvalidPhfDigestTest)
{
    msdo::download simpleDownload(g_smallFileUrl, g_tmpFileName);

    try
    {
        msdo::download_property_value integrityCheckInfo("blah");
        simpleDownload.set_property(msdo::download_property::integrity_check_info, integrityCheckInfo);
    }
    catch (const msdo::exception& e)
    {
        std::vector<int32_t> expectedErrors = { static_cast<int32_t>(msdo::errc::invalid_arg), static_cast<int32_t>(msdo::errc::do_e_unknown_property_id) };
        VerifyError(e.error_code(), expectedErrors);
        return;
    }
    ASSERT_TRUE(false);
}

TEST_F(DownloadPropertyTestsDOSVC, SmallDownloadWithCustomHeaders)
{
    msdo::download simpleDownload(g_smallFileUrl, g_tmpFileName);

    msdo::download_property_value httpCustomHeaders(std::string("XCustom1=someData\nXCustom2=moreData"));
    simpleDownload.set_property(msdo::download_property::http_custom_headers, httpCustomHeaders);

    simpleDownload.start_and_wait_until_completion();

    ASSERT_TRUE(boost::filesystem::exists(g_tmpFileName));
}

TEST_F(DownloadPropertyTestsDOSVC, CallbackTestUseDownload)
{
    msdo::download simpleDownload(g_largeFileUrl, g_tmpFileName);
    bool hitError = false;

    msdo::download_property_value callback([&hitError](msdo::download& download, msdo::download_status& status)
    {
        char msgBuf[1024];
        snprintf(msgBuf, sizeof(msgBuf), "Received status callback: %llu/%llu, 0x%x, 0x%x, %u",
            status.bytes_transferred(), status.bytes_total(), status.error_code(), status.extended_error_code(),
            static_cast<unsigned int>(status.state()));
        std::cout << msgBuf << std::endl;

        msdo::download_status status2 = download.get_status();
        if (hitError)
        {
            download.pause();
        }
    });

    simpleDownload.set_property(msdo::download_property::callback_interface, callback);
    simpleDownload.start();
    std::this_thread::sleep_for(5s);
    hitError = true;

    TestHelpers::WaitForState(simpleDownload, msdo::download_state::paused);
}

TEST_F(DownloadPropertyTestsDOSVC, SetCallbackTest)
{
    msdo::download simpleDownload(g_smallFileUrl, g_tmpFileName);

    int i= 0;
    msdo::download_property_value callback([&i](msdo::download& download, msdo::download_status& status)
        {
            char msgBuf[1024];
            snprintf(msgBuf, sizeof(msgBuf), "Received status callback: %llu/%llu, 0x%x, 0x%x, %u",
                status.bytes_transferred(), status.bytes_total(), status.error_code(), status.extended_error_code(),
                static_cast<unsigned int>(status.state()));
            std::cout << msgBuf << std::endl;
            i += 1;
        });
    simpleDownload.set_property(msdo::download_property::callback_interface, callback);

    simpleDownload.start_and_wait_until_completion();

    ASSERT_GE(i, 0);
}

TEST_F(DownloadPropertyTestsDOSVC, OverrideCallbackTest)
{
    msdo::download simpleDownload(g_smallFileUrl, g_tmpFileName);

    int i = 0;
    msdo::download_property_value callback([&i](msdo::download&, msdo::download_status&)
        {
            i += 1;
        });
    simpleDownload.set_property(msdo::download_property::callback_interface, callback);

    msdo::download_property_value::status_callback_t cb2 = [](msdo::download&, msdo::download_status&) {};

    callback = msdo::download_property_value(cb2);
    simpleDownload.set_property(msdo::download_property::callback_interface, callback);

    simpleDownload.start_and_wait_until_completion();

    ASSERT_EQ(i, 0);
}

TEST_F(DownloadPropertyTestsDOSVC, ForegroundBackgroundRace)
{
    double backgroundDuration = TimeOperation([&]()
        {
            msdo::download simpleDownload(g_largeFileUrl, g_tmpFileName);

            msdo::download_property_value foregroundPriority(false);
            simpleDownload.set_property(msdo::download_property::use_foreground_priority, foregroundPriority);

            simpleDownload.start_and_wait_until_completion();
        });

    printf("Time for background download: %f ms\n", backgroundDuration);
    double foregroundDuration = TimeOperation([&]()
        {
            msdo::download simpleDownload(g_largeFileUrl, g_tmpFileName2);

            msdo::download_property_value foregroundPriority(true);
            simpleDownload.set_property(msdo::download_property::use_foreground_priority, foregroundPriority);

            simpleDownload.start_and_wait_until_completion();
        });
    printf("Time for foreground download: %f ms\n", foregroundDuration);

    ASSERT_LT(foregroundDuration, backgroundDuration);
}