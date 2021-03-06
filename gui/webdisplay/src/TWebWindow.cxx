/// \file TWebWindow.cxx
/// \ingroup WebGui ROOT7
/// \author Sergey Linev <s.linev@gsi.de>
/// \date 2017-10-16
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2017, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "ROOT/TWebWindow.hxx"

#include "ROOT/TWebWindowsManager.hxx"
#include <ROOT/TLogger.hxx>

#include "THttpCallArg.h"
#include "THttpWSHandler.h"

#include <cstring>
#include <cstdlib>

namespace ROOT {
namespace Experimental {

/// just wrapper to deliver websockets call-backs to the TWebWindow class

class TWebWindowWSHandler : public THttpWSHandler {
public:
   TWebWindow &fDispl; ///<! display reference

   /// constructor
   TWebWindowWSHandler(TWebWindow &displ, const char *name)
      : THttpWSHandler(name, "TWebWindow websockets handler"), fDispl(displ)
   {
   }

   /// returns content of default web-page
   /// THttpWSHandler interface
   virtual TString GetDefaultPageContent() override { return fDispl.fDefaultPage.c_str(); }

   /// Process websocket request
   /// THttpWSHandler interface
   virtual Bool_t ProcessWS(THttpCallArg *arg) override { return arg ? fDispl.ProcessWS(*arg) : kFALSE; }
};

} // namespace Experimental
} // namespace ROOT

/** \class ROOT::Experimental::TWebWindow
\ingroup webdisplay

Represents web window, which can be shown in web browser or any other supported environment

Window can be configured to run either in the normal or in the batch (headless) mode.
In second case no any graphical elements will be created. For the normal window one can configure geometry
(width and height), which are applied when window shown.

Each window can be shown several times (if allowed) in different places - either as the
CEF (chromium embedded) window or in the standard web browser. When started, window will open and show
HTML page, configured with TWebWindow::SetDefaultPage() method.

Typically (but not necessarily) clients open web socket connection to the window and one can exchange data,
using TWebWindow::Send() method and call-back function assigned via TWebWindow::SetDataCallBack().

*/

ROOT::Experimental::TWebWindow::RawBuffer::~RawBuffer()
{
   if (fOwner && fBuffer)
      free((void *)fBuffer);
}

//////////////////////////////////////////////////////////////////////////////////////////
/// TWebWindow constructor
/// Should be defined here because of std::unique_ptr<TWebWindowWSHandler>

ROOT::Experimental::TWebWindow::TWebWindow() = default;

//////////////////////////////////////////////////////////////////////////////////////////
/// TWebWindow destructor
/// Closes all connections and remove window from manager

ROOT::Experimental::TWebWindow::~TWebWindow()
{
   fConn.clear();

   if (fMgr)
      fMgr->Unregister(*this);
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Configure window to show some of existing JSROOT panels
/// It uses "file:$jsrootsys/files/panel.htm" as default HTML page
/// At the moment only FitPanel is existing

void ROOT::Experimental::TWebWindow::SetPanelName(const std::string &name)
{
   if (!fConn.empty()) {
      R__ERROR_HERE("webgui") << "Cannot configure panel when connection exists";
   } else {
      fPanelName = name;
      SetDefaultPage("file:$jsrootsys/files/panel.htm");
   }
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Creates websocket handler, used for communication with the clients

void ROOT::Experimental::TWebWindow::CreateWSHandler()
{
   if (!fWSHandler)
      fWSHandler = std::make_unique<TWebWindowWSHandler>(*this, Form("win%u", GetId()));
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Return URL string to access web window
/// If remote flag is specified, real HTTP server will be started automatically

std::string ROOT::Experimental::TWebWindow::GetUrl(bool remote)
{
   return fMgr->GetUrl(*this, remote);
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Return THttpServer instance serving requests to the window

THttpServer *ROOT::Experimental::TWebWindow::GetServer()
{
   return fMgr->GetServer();
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Show window in specified location
/// See ROOT::Experimental::TWebWindowsManager::Show() docu for more info

bool ROOT::Experimental::TWebWindow::Show(const std::string &where)
{
   bool res = fMgr->Show(*this, where);
   if (res)
      fShown = true;
   return res;
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Processing of websockets call-backs, invoked from TWebWindowWSHandler

bool ROOT::Experimental::TWebWindow::ProcessWS(THttpCallArg &arg)
{
   if (arg.GetWSId() == 0)
      return kTRUE;

   // try to identify connection for given WS request
   WebConn *conn = nullptr;
   auto iter = fConn.begin();
   while (iter != fConn.end()) {
      if (iter->fWSId == arg.GetWSId()) {
         conn = &(*iter);
         break;
      }
      ++iter;
   }

   if (arg.IsMethod("WS_CONNECT")) {

      // refuse connection when limit exceed limit
      if (fConnLimit && (fConn.size() >= fConnLimit))
         return false;

      return true;
   }

   if (arg.IsMethod("WS_READY")) {
      if (conn) {
         R__ERROR_HERE("webgui") << "WSHandle with given websocket id " << arg.GetWSId() << " already exists";
         return false;
      }

      WebConn newconn;
      newconn.fWSId = arg.GetWSId();
      newconn.fConnId = ++fConnCnt; // unique connection id
      fConn.push_back(newconn);

      // CheckDataToSend();

      return true;
   }

   if (arg.IsMethod("WS_CLOSE")) {
      // connection is closed, one can remove handle

      if (conn && fDataCallback)
         fDataCallback(conn->fConnId, "CONN_CLOSED");

      if (conn)
         fConn.erase(iter);

      return true;
   }

   if (!arg.IsMethod("WS_DATA")) {
      R__ERROR_HERE("webgui") << "only WS_DATA request expected!";
      return false;
   }

   if (!conn) {
      R__ERROR_HERE("webgui") << "Get websocket data without valid connection - ignore!!!";
      return false;
   }

   if (arg.GetPostDataLength() <= 0)
      return true;

   // here processing of received data should be performed
   // this is task for the implemented windows

   const char *buf = (const char *)arg.GetPostData();
   char *str_end = nullptr;

   // printf("Get portion of data %d %.30s\n", (int)arg.GetPostDataLength(), buf);

   unsigned long ackn_oper = std::strtoul(buf, &str_end, 10);
   if (!str_end || *str_end != ':') {
      R__ERROR_HERE("webgui") << "missing number of acknowledged operations";
      return false;
   }

   unsigned long can_send = std::strtoul(str_end + 1, &str_end, 10);
   if (!str_end || *str_end != ':') {
      R__ERROR_HERE("webgui") << "missing can_send counter";
      return false;
   }

   unsigned long nchannel = std::strtoul(str_end + 1, &str_end, 10);
   if (!str_end || *str_end != ':') {
      R__ERROR_HERE("webgui") << "missing channel number";
      return false;
   }

   unsigned processed_len = (str_end + 1 - buf);

   if (processed_len > arg.GetPostDataLength()) {
      R__ERROR_HERE("webgui") << "corrupted buffer";
      return false;
   }

   std::string cdata(str_end + 1, arg.GetPostDataLength() - processed_len);

   conn->fSendCredits += ackn_oper;
   conn->fRecvCount++;
   conn->fClientCredits = (int)can_send;

   if (nchannel == 0) {
      // special default channel for basic communications
      if ((cdata == "READY") && !conn->fReady) {
         if (fPanelName.length()) {
            // initialization not yet finished, appropriate panel should be started
            Send(std::string("SHOWPANEL:") + fPanelName, conn->fConnId);
            conn->fReady = 5;
         } else {
            fDataCallback(conn->fConnId, "CONN_READY");
            conn->fReady = 10;
         }
      }
   } else if (fPanelName.length() && (conn->fReady < 10)) {
      if (cdata == "PANEL_READY") {
         R__DEBUG_HERE("webgui") << "Get panel ready " << fPanelName;
         fDataCallback(conn->fConnId, "CONN_READY");
         conn->fReady = 10;
      } else {
         fDataCallback(conn->fConnId, "CONN_CLOSED");
         fConn.erase(iter);
      }
   } else if (nchannel == 1) {
      fDataCallback(conn->fConnId, cdata);
   } else if (nchannel > 1) {
      conn->fCallBack(conn->fConnId, cdata);
   }

   CheckDataToSend();

   return true;
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Sends data via specified connection (internal use only)
/// Takes care about message prefix and account for send/recv credits

void ROOT::Experimental::TWebWindow::SendDataViaConnection(ROOT::Experimental::TWebWindow::WebConn &conn, int chid,
                                                           const std::string &data)
{
   if (!conn.fWSId || !fWSHandler) {
      R__ERROR_HERE("webgui") << "try to send text data when connection not established";
      return;
   }

   if (conn.fSendCredits <= 0) {
      R__ERROR_HERE("webgui") << "No credits to send text data via connection";
      return;
   }

   std::string buf;
   buf.reserve(data.length() + 100);

   buf.append(std::to_string(conn.fRecvCount));
   buf.append(":");
   buf.append(std::to_string(conn.fSendCredits));
   buf.append(":");
   conn.fRecvCount = 0; // we confirm how many packages was received
   conn.fSendCredits--;

   buf.append(std::to_string(chid));
   buf.append(":");

   // TODO: should we add extra : just as placeholder for any kind of custom data??
   buf.append(data);

   fWSHandler->SendCharStarWS(conn.fWSId, buf.c_str());
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Sends binary data via specified connection (internal use only)
/// Takes care about message prefix and account for send/recv credits

void ROOT::Experimental::TWebWindow::SendBinaryDataViaConnection(WebConn &conn, int chid,
                                                                 std::shared_ptr<RawBuffer> &data)
{
   if (!conn.fWSId || !fWSHandler) {
      R__ERROR_HERE("webgui") << "try to send binary data when connection not established";
      return;
   }

   if (conn.fSendCredits <= 0) {
      R__ERROR_HERE("webgui") << "No credits to send binary data via connection";
      return;
   }

   std::string buf;

   buf.append(std::to_string(conn.fRecvCount));
   buf.append(":");
   buf.append(std::to_string(conn.fSendCredits));
   buf.append(":");
   conn.fRecvCount = 0; // we confirm how many packages was received
   conn.fSendCredits--;

   buf.append(std::to_string(chid));
   buf.append(":");

   buf.append("$$binary$$");

   fWSHandler->SendHeaderWS(conn.fWSId, buf.c_str(), data->fBuffer, data->fLength);
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Checks if new data can be send (internal use only)
/// If necessary, provide credits to the client

void ROOT::Experimental::TWebWindow::CheckDataToSend(bool only_once)
{
   bool isany = false;

   do {
      isany = false;

      for (auto &&conn : fConn) {
         if (conn.fSendCredits <= 0)
            continue;

         if (!conn.fQueue.empty()) {
            QueueItem &item = conn.fQueue.front();
            if (item.fBuf)
               SendBinaryDataViaConnection(conn, item.fChID, item.fBuf);
            else
               SendDataViaConnection(conn, item.fChID, item.fMsg);
            conn.fQueue.erase(conn.fQueue.begin());
            isany = true;
         } else if ((conn.fClientCredits < 3) && (conn.fRecvCount > 1)) {
            // give more credits to the client
            R__DEBUG_HERE("webgui") << "Send keep alive to client";
            SendDataViaConnection(conn, 0, "KEEPALIVE");
            isany = true;
         }
      }

   } while (isany && !only_once);
}

///////////////////////////////////////////////////////////////////////////////////
/// Returns relative URL address for the specified window
/// Address can be required if one needs to access data from one window into another window
/// Used for instance when inserting panel into canvas

std::string ROOT::Experimental::TWebWindow::RelativeAddr(std::shared_ptr<TWebWindow> &win)
{
   if (fMgr != win->fMgr) {
      R__ERROR_HERE("WebDisplay") << "Same web window manager should be used";
      return "";
   }

   std::string res("../");
   res.append(win->fWSHandler->GetName());
   res.append("/");
   return res;
}

///////////////////////////////////////////////////////////////////////////////////
/// returns connection for specified connection number
/// Total number of connections can be retrieved with NumConnections() method

unsigned ROOT::Experimental::TWebWindow::GetConnectionId(int num) const
{
   auto iter = fConn.begin() + num;
   return iter->fConnId;
}

///////////////////////////////////////////////////////////////////////////////////
/// Closes all connection to clients
/// Normally leads to closing of all correspondent browser windows
/// Some browsers (like firefox) do not allow by default to close window

void ROOT::Experimental::TWebWindow::CloseConnections()
{
   Send("CLOSE", 0, 0);
}

///////////////////////////////////////////////////////////////////////////////////
/// Close specified connection
/// Connection id usually appears in the correspondent call-backs

void ROOT::Experimental::TWebWindow::CloseConnection(unsigned connid)
{
   if (connid)
      Send("CLOSE", connid, 0);
}

///////////////////////////////////////////////////////////////////////////////////
/// returns true if sending via specified connection can be performed
/// if direct==true, checks if direct sending (without queuing) is possible
/// if connid==0, all existing connections are checked

bool ROOT::Experimental::TWebWindow::CanSend(unsigned connid, bool direct) const
{
   for (auto &&conn : fConn) {
      if (connid && connid != conn.fConnId)
         continue;

      if (direct && (!conn.fQueue.empty() || (conn.fSendCredits == 0)))
         return false;

      if (conn.fQueue.size() >= fMaxQueueLength)
         return false;
   }

   return true;
}

///////////////////////////////////////////////////////////////////////////////////
/// Sends data to specified connection/channel
/// If connid==0, data will be send to all connections
/// Normally chid==1 (chid==0 is reserved for internal communications)

void ROOT::Experimental::TWebWindow::Send(const std::string &data, unsigned connid, unsigned chid)
{
   for (auto &&conn : fConn) {
      if (connid && connid != conn.fConnId)
         continue;

      if (conn.fQueue.empty() && (conn.fSendCredits > 0)) {
         SendDataViaConnection(conn, chid, data);
      } else if (conn.fQueue.size() < fMaxQueueLength) {
         conn.fQueue.emplace_back(chid, data);
      } else {
         R__ERROR_HERE("webgui") << "Maximum queue length achieved";
      }
   }

   CheckDataToSend();
}

///////////////////////////////////////////////////////////////////////////////////
/// Send binary data to specified connection
/// If connid==0, data will be sent to all connections

void ROOT::Experimental::TWebWindow::SendBinary(std::shared_ptr<RawBuffer> &data, unsigned connid, unsigned chid)
{
   for (auto &&conn : fConn) {
      if (connid && connid != conn.fConnId)
         continue;

      if (conn.fQueue.empty() && (conn.fSendCredits > 0)) {
         SendBinaryDataViaConnection(conn, chid, data);
      } else if (conn.fQueue.size() < fMaxQueueLength) {
         conn.fQueue.emplace_back(chid, data);
      } else {
         R__ERROR_HERE("webgui") << "Maximum queue length achieved";
      }
   }

   CheckDataToSend();
}

/////////////////////////////////////////////////////////////////////////////////
/// Set call-back function for data, received from the clients via websocket
/// Function should have signature like void func(unsigned connid, const std::string &data)
/// First argument identifies connection (unique for each window), second argument is received data
/// There are predefined values for the data:
///     "CONN_READY"  - appears when new connection is established
///     "CONN_CLOSED" - when connection closed, no more data will be send/received via connection
/// Most simple way to assign call-back - use of c++11 lambdas like:
/// ~~~ {.cpp}
/// std::shared_ptr<TWebWindow> win = TWebWindowsManager::Instance()->CreateWindow();
/// win->SetDefaultPage("file:./page.htm");
/// win->SetDataCallBack([](unsigned connid, const std::string &data) { printf("Conn:%u data:%s\n", connid,
/// data.c_str()); });
/// win->Show("opera");
/// ~~~

void ROOT::Experimental::TWebWindow::SetDataCallBack(WebWindowDataCallback_t func)
{
   fDataCallback = func;
}

/////////////////////////////////////////////////////////////////////////////////
/// Waits until provided check function or lambdas returns non-zero value
/// Runs application mainloop and short sleeps in-between
/// timelimit (in seconds) defines how long to wait (0 - forever)
/// Function has following signature: int func(double spent_tm)
/// Parameter spent_tm is time in seconds, which already spent inside function
/// Waiting will be continued, if function returns zero.
/// First non-zero value breaks waiting loop and result is returned (or 0 if time is expired).

int ROOT::Experimental::TWebWindow::WaitFor(WebWindowWaitFunc_t check, double timelimit)
{
   return fMgr->WaitFor(check, timelimit);
}
