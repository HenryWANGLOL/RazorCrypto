#include "RT_CryptoMDBase.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <thread>
#include <iostream>
using namespace std;
using namespace mht_rt;


RT_CryptoMDBase::RT_CryptoMDBase(rapidjson::Document &json_config) : RTModuleBase(json_config)
{

    logInfo("======================SUNNY TRADE MD INIT =========================");
};

int RT_CryptoMDBase::initialize()
{
    request_connect();
    while (!m_bDoneConnect)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    
    }
    return 0;

}

void RT_CryptoMDBase::start() {
    while (1) {
        ;
    }
}