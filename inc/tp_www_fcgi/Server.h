#ifndef tp_www_fcgi_Server_h
#define tp_www_fcgi_Server_h

#include "tp_www_fcgi/Globals.h"

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
public:

  //################################################################################################
  Server(tp_www::Route* root);

  //################################################################################################
  void exec(int threadCount=1);
};

}

#endif






