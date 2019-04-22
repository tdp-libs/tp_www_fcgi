#ifndef tp_www_fcgi_Server_h
#define tp_www_fcgi_Server_h

#include "tp_www_fcgi/Globals.h"

#include "tp_utils/CallbackCollection.h"

namespace tp_www
{
class Route;
}

namespace tp_www_fcgi
{

//##################################################################################################
class Server
{
  tp_www::Route* m_root;
  std::vector<std::function<void()>> m_eventCallbacks;
public:

  //################################################################################################
  Server(tp_www::Route* root);

  //################################################################################################
  void exec(int threadCount=1);

  //################################################################################################
  //! Polled on each itteration of the exec event loop.
  tp_utils::CallbackCollection<void()> pollEventCallback;
};

}

#endif






