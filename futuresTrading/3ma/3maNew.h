#ifndef _3MA_H_
#define _3MA_H_

#include <kLineGenerator.h>
#include <futuresTradeClient.h>
#include <algorithm>

#define LOAD_LAST_POSITION_SQL	"select * from futuresTradingRecord.%1% where strategyId = '%2%' and date < '%3%' order by date desc limit 0, 1"
#define SAVE_RECORD_SQL		"replace into futuresTradingRecord.%1%(strategyId, symbol, date, state, size, basisPrice, trailingMaxPnl, memo) values('%2%', '%3%', '%4%', %5%, %6%, %7%, %8%, '')"
#define SELECT_KLINE_SQL	"select * from futures.%1% where symbol='%2%' and period = 5 order by time asc"

template<LaunchMode mode>
class ThreeMAClass: public KLineGenerator, public FuturesTradeClient<mode> {
private:
  double realizedPnl = 0;

  string POSITION_RECORD_TABLE;
  string TRADE_RECORD_TABLE;
  enum State {
    NoPosition,
    LongWatching,
    Long,
    LongTrailingStarted,
    ShortWatching,
    Short,
    ShortTrailingStarted
  };

  enum Condition {
    None,
    LongD,
    ShortD
  };

  string stateToString(State state) {
    switch (state) {
    case NoPosition: return"NoPosition ";
    case Long: return "Long ";
    case Short: return "Short ";
    case LongWatching: return "LongWatching ";
    case ShortWatching: return "ShortWatching ";
    case LongTrailingStarted: return "LongTrailingStarted";
    case ShortTrailingStarted: return "ShortTrailingStarted";
    default: return "WrongState";
    }
  }

  string DirectionToString(Condition direction) {
    switch (direction) {
    case None: return"None ";
    case LongD: return "LongD ";
    case ShortD: return "ShortD ";
    default: return "WrongState";
    }
  }

public:
  ThreeMAClass(const FuturesConfigInfo& configInfo);

  void fillLastNDaysKLineMysql(int n);
  void loadLastPosition();
  string getStrategyId();
  void saveTodayPosition();
  bool curStateSaved = false;

  void dumpStatus(CThostFtdcDepthMarketDataField * p = NULL);

  void dumpParam();

  virtual void OnNotOneMinuteKLineInserted(int periodIndex);

  virtual void OnOneMinuteKLineInserted();

  virtual void onTradeEnd();

  void calculateParameters();

  void returnToNoPositionState();

  virtual void onFeed(CThostFtdcDepthMarketDataField *pFeed);

  State state = NoPosition;

  double anchorKLineHigh = 0;
  double anchorKLineLow = 0;

  string maxPnlTime;
  int maxPnlMillisec;
  double maxPnlPrice;

  int X = 20;
  int Y = 60;
  int m = 20;
  int n = 15;
  int adjust = 10;
  int trailing = 10;
  double stopLoss = 10;
  int f1 = 10;
  int f2 = 20;
  int f3 = 30;

  double MA1 = 0;
  double MA2 = 0;
  double Hi = 0;
  double Lo = 0;
  double MAF1 = 0;
  double MAF2 = 0;
  double MAF3 = 0;

  double newH = 0;
  int countH = -1;
  double newL = 0;
  int countL = -1;
  double trailingMaxPnl = 0;

  double tenKLineL = 0;
  double tenKLineH = 10000;

  double basisPrice = 0.0;
  int nTrades = 0;

  int nContractsTraded;
  double accumulatedPnlBeforeCommission;
  double accumulatedPnlAfterCommission;

  ~ThreeMAClass() {}
};

template<LaunchMode mode>
void ThreeMAClass<mode>::fillLastNDaysKLineMysql(int n) {
  string query = "show tables from futures where Tables_in_futures < " + date;
  ResultSet *res = mysqlConnector.query(query);
  if (!res) {
      BOOST_LOG_SEV(lg, fatal) << "DB query last trading day failed";
      exit(-1);
  }
  vector<string> tradingDays;
  while (res->next()) {
      string lastTradingDay = res->getString(1);
      tradingDays.push_back(lastTradingDay);
  }

  for (int i = min((size_t)n, tradingDays.size()); i > 0; --i) {
      string tableName = tradingDays[tradingDays.size() - i];
      string query = (boost::format(SELECT_KLINE_SQL) % tableName % symbol).str();

      vector<KLine> &fiveMinuteKLines = notOneMinuteKLines[0];
      res = mysqlConnector.query(query);
      while (res && res->next()) {
	  KLine oneKLine;
	  strcpy(oneKLine.date, tableName.substr(0, 8).c_str());
	  strcpy(oneKLine.symbol, symbol.c_str());
	  oneKLine.open = res->getDouble("open");
	  oneKLine.high = res->getDouble("high");
	  oneKLine.low = res->getDouble("low");
	  oneKLine.close = res->getDouble("close");
	  oneKLine.volume = res->getInt("volume");
	  strcpy(oneKLine.time, res->getString("time").c_str());
	  fiveMinuteKLines.push_back(oneKLine);
	  calculateParameters();
	}
    }
}

template<LaunchMode mode>
string ThreeMAClass<mode>::getStrategyId() {
  return "130";
}

template<LaunchMode mode>
ThreeMAClass<mode>::ThreeMAClass(const FuturesConfigInfo &config) :
KLineGenerator(config.simDate == "" ? FuturesUtil::getCurrentDateString() : config.simDate, config.Symbol, vector<int>(1, 5), config.simDate == ""), FuturesTradeClient<mode>(config) {
  FuturesTradeClient<mode>::iRequestID = 130000000;
  if(config.simDate != "") {
    POSITION_RECORD_TABLE = "dailyPositionRecordSim";
    TRADE_RECORD_TABLE = "dailyTradeRecordSim";
  } else {
    POSITION_RECORD_TABLE = "dailyPositionRecord";
    TRADE_RECORD_TABLE = "dailyTradeRecord";
  }
  fillLastNDaysKLineMysql(7);
  loadLastPosition();
}


template<LaunchMode mode>
void ThreeMAClass<mode>::loadLastPosition() {
  string query = (boost::format(LOAD_LAST_POSITION_SQL) % POSITION_RECORD_TABLE % getStrategyId() % date).str();
  ResultSet* res = mysqlConnector.query(query);
  if (res && res->next())
  {
  state = (State)res->getInt("state");
  this->size = res->getInt("size");
  basisPrice = res->getDouble("basisPrice");
  trailingMaxPnl = res->getDouble("trailingMaxPnl");
  LOG_BOOST << "3MA " << "Load last position: " << stateToString(state) << " basisPrice: " << basisPrice << " trailingMaxPnl: " << trailingMaxPnl;
  }
}

template<LaunchMode mode>
void ThreeMAClass<mode>::saveTodayPosition() {
  string query = (boost::format(SAVE_RECORD_SQL)
    % POSITION_RECORD_TABLE % getStrategyId() % symbol % date % state % this->size  % basisPrice % trailingMaxPnl).str();
  mysqlConnector.executeUpdate(query);
  LOG_BOOST << query;
}

template<LaunchMode mode>
void ThreeMAClass<mode>::dumpParam() {
  stringstream ss;
  ss << notOneMinuteKLines[0].back().date << "," << notOneMinuteKLines[0].back().time << ","
    << notOneMinuteKLines[0].back().open << "," << notOneMinuteKLines[0].back().high << ","
    << notOneMinuteKLines[0].back().low << "," << notOneMinuteKLines[0].back().close << "," << notOneMinuteKLines[0].back().volume << ","
    << MA1 << "," << MA2 << "," << MAF1 << "," << MAF2 << ","
    << MAF3 << "," << Hi << "," << Lo << "," << newH << "," << countH << "," << newL << "," << countL << "," << tenKLineH << "," << tenKLineL << endl;
  LOG_BOOST << "3MA " << ss.str();
}

template<LaunchMode mode>
void ThreeMAClass<mode>::dumpStatus(CThostFtdcDepthMarketDataField * p) {
  stringstream ss;
  ss << "Time: " << notOneMinuteKLines[0].back().date << " " << notOneMinuteKLines[0].back().time << " " << p->UpdateTime
    << " state: " << stateToString(state)
    << " MA1: " << MA1
    << " MA2: " << MA2
    << " MAF1: " << MAF1
    << " MAF2: " << MAF2
    << " MAF3: " << MAF3
    << " Hi: " << Hi
    << " Lo: " << Lo
    << " newH: " << newH
    << " countH: " << countH
    << " newL: " << newL
    << " countL: " << countL
    << " trailingMaxPnl: " << trailingMaxPnl
    << " basisPrice: " << basisPrice
    << " tenKLineH: " << tenKLineH
    << " tenKLineL: " << tenKLineL
    << " last price: " << p->LastPrice
    << " numKLines: " << notOneMinuteKLines[0].size()
    << " Last KLine: " << notOneMinuteKLines[0].back().toString();
  LOG_BOOST << "3MA " << ss.str();
}

template<LaunchMode mode>
void ThreeMAClass<mode>::OnNotOneMinuteKLineInserted(int periodIndex) {
  calculateParameters();
  vector<KLine> &fiveMinuteKLines = notOneMinuteKLines[0];
  KLine &lastKLine = fiveMinuteKLines.back();
  switch (state)
  {
  case NoPosition:
  case ShortWatching:
  case LongWatching: {
               if (lastKLine.close > MA1 && MA1 > MA2 && lastKLine.high > Hi && lastKLine.close < (Hi + adjust)) {
                 anchorKLineHigh = lastKLine.high;
                 state = LongWatching;
                 LOG_BOOST << "3MA " << "State change to Long watching, At Time--->" << curTick.UpdateTime << endl;
               }
               else if (lastKLine.close < MA1 && MA1 < MA2 && lastKLine.low < Lo && lastKLine.close >(Lo - adjust)) {
                 anchorKLineLow = lastKLine.low;
                 state = ShortWatching;
                 LOG_BOOST << "3MA " << "State change to Short watching, At Time--->" << curTick.UpdateTime << endl;
               }
               else {
                 state = NoPosition;
                 LOG_BOOST << "3MA " << "Cur state: NoPosition , At Time--->" << curTick.UpdateTime << endl;
               }
               break;
  }
  case Long:
  case LongTrailingStarted: {
                  if (countH == -1) {
                    newH = lastKLine.high;
                    countH = 0;
                  }
                  else if (lastKLine.high > newH) {
                    newH = lastKLine.high;
                    ++countH;
                  }

                  tenKLineL = 1000000;
                  for (int i = 0; i < 10; ++i)
                    tenKLineL = min(tenKLineL, fiveMinuteKLines[fiveMinuteKLines.size() - 1 - i].low);
                  LOG_BOOST << "3MA " << "Long state, update newH, countH, tenKLineL, At Time--->" << curTick.UpdateTime << endl;
                  break;
  }
  case Short:
  case ShortTrailingStarted: {
                   if (countL == -1) {
                     newL = lastKLine.low;
                     countL = 0;
                   }
                   else if (lastKLine.low < newL) {
                     newL = lastKLine.low;
                     ++countL;
                   }

                   tenKLineH = 0;
                   for (int i = 0; i < 10; ++i)
                     tenKLineH = max(tenKLineH, fiveMinuteKLines[fiveMinuteKLines.size() - 1 - i].high);

                   LOG_BOOST << "3MA " << "Short state, update newL, countL, tenKLineH, At Time--->" << curTick.UpdateTime << endl;
                   break;
  }
  default:
    break;
  }
  dumpStatus(&curTick);
}


template<LaunchMode mode>
void ThreeMAClass<mode>::calculateParameters() {
  vector<KLine> &fiveMinuteKLines = notOneMinuteKLines[0];
  int numKLines = fiveMinuteKLines.size();

  // f1 < X = f2 < f3 < Y
  if (numKLines >= Y)
  {
    MA1 = MA2 = MAF1 = MAF2 = MAF3 = 0;	Hi = 0; Lo = 1000000;
    int i = 0;
    for (; i < f1; ++i)
    {
      MA1 += fiveMinuteKLines[numKLines - 1 - i].close;
      MA2 += fiveMinuteKLines[numKLines - 1 - i].close;
      MAF1 += fiveMinuteKLines[numKLines - 1 - i].close;
      MAF2 += fiveMinuteKLines[numKLines - 1 - i].close;
      MAF3 += fiveMinuteKLines[numKLines - 1 - i].close;
    }
    for (; i < f2; ++i)
    {
      MA1 += fiveMinuteKLines[numKLines - 1 - i].close;
      MA2 += fiveMinuteKLines[numKLines - 1 - i].close;
      MAF2 += fiveMinuteKLines[numKLines - 1 - i].close;
      MAF3 += fiveMinuteKLines[numKLines - 1 - i].close;
    }
    for (; i < f3; ++i)
    {
      MA2 += fiveMinuteKLines[numKLines - 1 - i].close;
      MAF3 += fiveMinuteKLines[numKLines - 1 - i].close;
    }
    for (; i < Y; ++i)
      MA2 += fiveMinuteKLines[numKLines - 1 - i].close;
    MA1 /= X;
    MA2 /= Y;
    MAF1 /= f1;
    MAF2 /= f2;
    MAF3 /= f3;

    for (i = 1; i <= n; ++i)
    {
      Hi = max(Hi, fiveMinuteKLines[numKLines - 1 - i].high);
      Lo = min(Lo, fiveMinuteKLines[numKLines - 1 - i].low);
    }
    for (; i <= m; ++i)
      Hi = max(Hi, fiveMinuteKLines[numKLines - 1 - i].high);
  }
  dumpParam();
}

template<LaunchMode mode>
void ThreeMAClass<mode>::returnToNoPositionState() {
  state = NoPosition;
  newH = 0;
  countH = -1;
  newL = 0;
  countL = -1;
  trailingMaxPnl = 0;
  tenKLineL = 0;
  tenKLineH = 10000;
}

template<LaunchMode mode>
void ThreeMAClass<mode>::onFeed(CThostFtdcDepthMarketDataField * p) {
  switch (state)
  {
  case NoPosition:
    break;
  case LongWatching: {
               if (anchorKLineHigh <= p->LastPrice) {
                 this->insertOrder(symbol, 0.0, this->size, OPEN_LONG);
                 basisPrice = p->AskPrice1;
                 state = Long;
                 LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Open Long BasisPrice----->" << basisPrice << endl;
                 dumpStatus(p);
                 saveTodayPosition();
               }
               break;
  }
  case Long:
  case LongTrailingStarted: {
                  /*if (0 <= countH && countH < 3) {
                  if (p->LastPrice <= basisPrice - stopLoss) {
                  this->insertOrder(symbol, 0.0, size, CLOSE_LONG);
                  returnToNoPositionState();
                  LOG_BOOST << "3MA " << "StopLoss/Profit Long case 1, At Time--->" << p->UpdateTime << "   Pnl----->" << p->BidPrice1 - basisPrice << endl;
                  dumpStatus(p);
                  break;
                  }
                  }
                  else */
                  if (3 <= countH && countH < 6) {
                    if (p->LastPrice <= MAF3) {
                      this->insertOrder(symbol, 0.0, this->size, CLOSE_LONG);
                      returnToNoPositionState();
                      realizedPnl += p->BidPrice1 - basisPrice;
                      LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Close Long StopLoss/Profit case 2@" << p->BidPrice1 << " , Pnl----->" << p->BidPrice1 - basisPrice << endl;
                      dumpStatus(p);
                      saveTodayPosition();
                      break;
                    }
                  }
                  else if (6 <= countH && countH < 9) {
                    if (p->LastPrice <= MAF2) {
                      this->insertOrder(symbol, 0.0, this->size, CLOSE_LONG);
                      returnToNoPositionState();
                      realizedPnl += p->BidPrice1 - basisPrice;
                      LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Close Long StopLoss/Profit case 3@" << p->BidPrice1 << " , Pnl----->" << p->BidPrice1 - basisPrice << endl;
                      dumpStatus(p);
                      saveTodayPosition();
                      break;
                    }
                  }
                  else if (9 <= countH) {
                    if (p->LastPrice <= MAF1) {
                      this->insertOrder(symbol, 0.0, this->size, CLOSE_LONG);
                      returnToNoPositionState();
                      realizedPnl += p->BidPrice1 - basisPrice;
                      LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Close Long StopLoss/Profit case 4@" << p->BidPrice1 << " ,  Pnl----->" << p->BidPrice1 - basisPrice << endl;
                      dumpStatus(p);
                      saveTodayPosition();
                      break;
                    }
                  }
                  if (p->LastPrice <= tenKLineL || p->LastPrice <= basisPrice - stopLoss * 2) {
                    this->insertOrder(symbol, 0.0, this->size, CLOSE_LONG);
                    returnToNoPositionState();
                      realizedPnl += p->BidPrice1 - basisPrice;
                    LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Close Long StopLoss/Profit case 5@" << p->BidPrice1 << " , Pnl----->" << p->BidPrice1 - basisPrice << endl;
                    dumpStatus(p);
                    saveTodayPosition();
                    break;
                  }
                  if (state == Long && p->LastPrice - basisPrice >= trailing) {
                    state = LongTrailingStarted;
                    trailingMaxPnl = p->LastPrice - basisPrice;
                    maxPnlTime = p->UpdateTime;
                    maxPnlMillisec = p->UpdateMillisec;
                    maxPnlPrice = p->LastPrice;
                    LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Move to LongTrailingStarted phase" << endl;
                    dumpStatus(p);
                    saveTodayPosition();
                  }
                  else if (state == LongTrailingStarted) {
                    if (p->LastPrice - basisPrice > trailingMaxPnl) {
                      maxPnlTime = p->UpdateTime;
                      maxPnlMillisec = p->UpdateMillisec;
                      maxPnlPrice = p->LastPrice;
                      trailingMaxPnl = p->LastPrice - basisPrice;
                    }
                    else if (floatNumberLessEqual(p->LastPrice - basisPrice, 0.4 * trailingMaxPnl))
                    {
                      this->insertOrder(symbol, 0.0, this->size, CLOSE_LONG);
                      LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Close Long, Trailing StopLoss/Profit@" << p->BidPrice1 << " , Pnl----->" << p->BidPrice1 - basisPrice << "MaxPnl----->" << trailingMaxPnl << " MaxPnlTime----->" << maxPnlTime << ":" << maxPnlMillisec << " price:" << maxPnlPrice << endl;
                      realizedPnl += p->BidPrice1 - basisPrice;
                      returnToNoPositionState();
                      dumpStatus(p);
                      saveTodayPosition();
                    }
                  }
                  break;
  }
  case ShortWatching: {
              if (floatNumberLessEqual(p->LastPrice, anchorKLineLow))
              {
                this->insertOrder(symbol, 0.0, this->size, OPEN_SHORT);
                basisPrice = p->BidPrice1;
                state = Short;
                LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Open Short, BasisPrice----->" << basisPrice << endl;
                dumpStatus(p);
                saveTodayPosition();
              }
              break;
  }
  case Short:
  case ShortTrailingStarted:{
                  /*if (0 <= countL && countL < 3) {
                  if (p->LastPrice >= basisPrice + stopLoss) {
                  this->insertOrder(symbol, 0.0, size, CLOSE_SHORT);
                  returnToNoPositionState();
                  LOG_BOOST << "3MA " << "StopLoss/Profit Short case 1, At Time--->" << p->UpdateTime << "   Pnl----->" << basisPrice - p->AskPrice1 << endl;
                  dumpStatus(p);
                  break;
                  }
                  }
                  else */
                  if (3 <= countL && countL < 6) {
                    if (p->LastPrice >= MAF3) {
                      this->insertOrder(symbol, 0.0, this->size, CLOSE_SHORT);
                      returnToNoPositionState();
                      realizedPnl += basisPrice - p->AskPrice1;
                      LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Close Short StopLoss/Profit case 2@" << p->AskPrice1 << " ,  Pnl----->" << basisPrice - p->AskPrice1 << endl;
                      dumpStatus(p);
                      saveTodayPosition();
                      break;
                    }
                  }
                  else if (6 <= countL && countL < 9) {
                    if (p->LastPrice >= MAF2) {
                      this->insertOrder(symbol, 0.0, this->size, CLOSE_SHORT);
                      returnToNoPositionState();
                      realizedPnl += basisPrice - p->AskPrice1;
                      LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Close Short StopLoss/Profit case 3@" << p->AskPrice1 << " ,  Pnl----->" << basisPrice - p->AskPrice1 << endl;
                      dumpStatus(p);
                      saveTodayPosition();
                      break;
                    }
                  }
                  else if (9 <= countL) {
                    if (p->LastPrice >= MAF1) {
                      this->insertOrder(symbol, 0.0, this->size, CLOSE_SHORT);
                      returnToNoPositionState();
                      realizedPnl += basisPrice - p->AskPrice1;
                      LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Close Short StopLoss/Profit case 4@" << p->AskPrice1 << " , Pnl----->" << basisPrice - p->AskPrice1 << endl;
                      dumpStatus(p);
                      saveTodayPosition();
                      break;
                    }
                  }
                  if (p->LastPrice >= tenKLineH || p->LastPrice >= basisPrice + stopLoss * 2) {
                    this->insertOrder(symbol, 0.0, this->size, CLOSE_SHORT);
                    returnToNoPositionState();
                    realizedPnl += basisPrice - p->AskPrice1;
                    LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Close Short StopLoss/Profit case 5@" << p->AskPrice1 << " ,  Pnl----->" << basisPrice - p->AskPrice1 << endl;
                    dumpStatus(p);
                    saveTodayPosition();
                    break;
                  }
                  if (state == Short && basisPrice - p->LastPrice >= trailing) {
                    state = ShortTrailingStarted;
                    trailingMaxPnl = basisPrice - p->LastPrice;
                    maxPnlTime = p->UpdateTime;
                    maxPnlMillisec = p->UpdateMillisec;
                    maxPnlPrice = p->LastPrice;
                    LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Move to ShortTrailingStarted, curTrailing Pnl----->" << basisPrice - p->AskPrice1 << endl;
                    dumpStatus(p);
                    saveTodayPosition();
                  }
                  else if (state == ShortTrailingStarted) {
                    if (basisPrice - p->LastPrice > trailingMaxPnl) {
                      trailingMaxPnl = basisPrice - p->LastPrice;
                      maxPnlTime = p->UpdateTime;
                      maxPnlMillisec = p->UpdateMillisec;
                      maxPnlPrice = p->LastPrice;
                    }
                    else if (floatNumberLessEqual(basisPrice - p->LastPrice, 0.4 * trailingMaxPnl))
                    {
                      this->insertOrder(symbol, 0.0, this->size, CLOSE_SHORT);
                      realizedPnl += basisPrice - p->AskPrice1;
                      LOG_BOOST << "3MA " << FuturesUtil::futuresTickToString(p) << " Close Short Trailing StopLoss/Profit@" << p->AskPrice1 << " , Pnl----->" << basisPrice - p->AskPrice1 << "MaxPnl----->" << trailingMaxPnl << " MaxPnlTime----->" << maxPnlTime << ":" << maxPnlMillisec << " price:" << maxPnlPrice << endl;
                      returnToNoPositionState();
                      dumpStatus(p);
                      saveTodayPosition();
                    }
                  }
                  break;
  }
  default:
    break;
  }

  KLineGenerator::feedTickData(p);
}

template<LaunchMode mode>
void ThreeMAClass<mode>::OnOneMinuteKLineInserted() {
  cout << "Tick: " << this->lastTick.UpdateTime << " kline time: " << lastOneMinuteKLineTime << " kline:" << lastOneMinuteKLine.toString() << endl;
}


template<LaunchMode mode>
void ThreeMAClass<mode>::onTradeEnd() {
  cout << realizedPnl;
}





#endif
