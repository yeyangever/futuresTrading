#include "../include/FuturesMdSpi.h"


FeedHandler::FeedHandler(const FuturesConfigInfo& _configInfo) : config(_configInfo), lock2(mtx2, defer_lock) {
	pMdApi = CThostFtdcMdApi::CreateFtdcMdApi();
	pMdApi->RegisterSpi(this);

	pMdApi->RegisterFront(const_cast<char*>(FeedHandler::config.MdFrontAddr.c_str()));
	pMdApi->Init();
}

void FeedHandler::OnFrontConnected() {
	CThostFtdcReqUserLoginField req;
	memset(&req, 0, sizeof(req));
	strcpy(req.BrokerID, config.MdBrokerId.c_str());
	strcpy(req.UserID, config.MdUserId.c_str());
	strcpy(req.Password, config.MdPassword.c_str());
	pMdApi->ReqUserLogin(&req, 0);
	cerr << "--->>> Connect to Md Frontend: " << ((iResult == 0) ? "Succeed" : "Failure") << endl;
}


void FeedHandler::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
	cerr << "--->>> " << __FUNCTION__ << endl;
	if (bIsLast)
	{
		std::unique_lock<mutex> localLock(mtx2);
		cv2.notify_all();
		localLock.unlock();
		SubscribeMarketData(config.Symbol);
	}	
}


int FeedHandler::SubscribeMarketData(const vector<string>& instruments) {
	int count = instruments.size();
	char** pInstruments = new char*[count];
	for (int i = 0; i < instrument.size(); ++i)
	{
		pInstruments[i] = new char[20];
		strcpy(pInstruments[i], 20, instruments[i].c_str());
	}

	int ret = pMdApi->SubscribeMarketData(pInstruments, instruments.size());
	for (int i = 0; i < count; ++i)
		delete pInstruments[i];
	delete pInstruments;

	return ret;
}


int FeedHandler::SubscribeMarketData(const string& instrument) {
	vector<string> instruments(1, instrument);
	return SubscribeMarketData(instruments);
}
