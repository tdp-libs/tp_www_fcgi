#include "tp_www_fcgi/Server.h"

#include "tp_www/Request.h"
#include "tp_www/Route.h"

#include "tp_utils/FileUtils.h"
#include "tp_utils/DebugUtils.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/iter_find.hpp>
#include <boost/algorithm/string/finder.hpp>
#include <boost/utility/string_view.hpp>

//dnf install fcgi-devel nginx spawn-fcgi -y
#include <fcgio.h>

#include <fstream>
#include <thread>

#include <vector>

namespace tp_www_fcgi
{

namespace
{

//##################################################################################################
void decodeURL(std::string& text)
{
  if(text.empty())
    return;

  char*       o = &text[0];
  const char* i = o;
  const char* iMax = i + text.size();
  for(; i<iMax; i++, o++)
  {
    if(((*i)>='a' && (*i)<='z') ||
       ((*i)>='A' && (*i)<='Z') ||
       ((*i)>='0' && (*i)<='9') ||
       (*i) == '-'              ||
       (*i) == '_'              ||
       (*i) == '.'              ||
       (*i) == '~')
    {
      (*o) = (*i);
      continue;
    }

    if((*i) == '+')
    {
      (*o) = ' ';
      continue;
    }

    if((*i) == '%')
    {
      auto fromHex = [](char a)->char
      {
        if(a>='a' && a<='f')return 10+(a-'a');
        if(a>='A' && a<='F')return 10+(a-'A');
        if(a>='0' && a<='9')return a-'0';
        return 0;
      };

      i++;
      if(i>=iMax)
        break;

      char val = char(fromHex(*i)<<4);

      i++;
      if(i>=iMax)
        break;

      val += fromHex(*i);
      (*o) = val;

      continue;
    }
  }

  size_t newSize = size_t(o-(&text[0]));
  text.resize(newSize);
}

//##################################################################################################
void splitParams(const std::string& content, std::unordered_map<std::string, std::string>& params)
{
  std::vector<std::string> paramPairs;
  boost::split(paramPairs, content, [](char a){return a=='&';}, boost::token_compress_on);

  for(const std::string& paramPair : paramPairs)
  {
    std::vector<std::string> parts;
    boost::split(parts, paramPair, [](char a){return a=='=';}, boost::token_compress_on);

    if(parts.size() == 2)
    {
      std::string key   = parts.at(0);
      std::string value = parts.at(1);

      decodeURL(key);
      decodeURL(value);

      params[key] = value;
    }
  }
}

//##################################################################################################
bool parseMultipartParam(std::ostream& err, const boost::string_view& content, tp_www::MultipartFormData& param)
{
  size_t headerEnd = content.find("\r\n\r\n");
  if(headerEnd>=content.size())
    return false;

  size_t contentStart = headerEnd+4;

  std::vector<std::string> headders;
  tpSplit(headders, content.substr(0, headerEnd).to_string(), "\r\n");

  for(const std::string& headder : headders)
  {
    if(headder.size()<1)
      continue;

    size_t del = headder.find(':');
    if(del<1 || del>=headder.size())
    {
      err << "Fail::: " << headder << "\n";
      return false;
    }

    std::string key = headder.substr(0, del);
    del++;
    std::string value = headder.substr(del, headder.size()-del);

    param.headers[key] = value;
  }

  param.contentType = tpGetMapValue(param.headers, "Content-Type");
  tpRemoveChar(param.contentType, ' ');

  {
    //https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Disposition
    //Content-Disposition: form-data; name="file-0"; filename="pic.png
    std::string line = tpGetMapValue(param.headers, "Content-Disposition");

    std::vector<std::string> parts;
    tpSplit(parts, line, ";");

    //inline
    //attachment
    //form-data
    if(!parts.empty())
    {
      param.contentDisposition = parts.at(0);
      tpRemoveChar(param.contentDisposition, ' ');
    }

    for(size_t i=1; i<parts.size(); i++)
    {
      std::string part = parts.at(i);
      tpRemoveChar(part, ' ');

      if(tpStartsWith(part, "name="))
      {
        param.name = part.substr(5);
        tpRemoveChar(param.name, '"');
      }
      else if(tpStartsWith(part, "filename="))
      {
        param.filename = part.substr(9);
        tpRemoveChar(param.filename, '"');
      }
    }
  }

  param.data = content.substr(contentStart).to_string();
  return true;
}

//##################################################################################################
//RFC 7578 	Returning Values from Forms: multipart/form-data
//RFC 6266 	Use of the Content-Disposition Header Field in the Hypertext Transfer Protocol (HTTP)
//RFC 2183 	Communicating Presentation Information in Internet Messages: The Content-Disposition Header Field
bool splitMultipartParams(std::ostream& err,
                          const std::string& content,
                          const std::string& contentType,
                          std::unordered_map<std::string, tp_www::MultipartFormData>& params)
{
  std::vector<std::string> splitContentType;
  //It turns out that in some cases the boundary sequence is assembled from a number of base64
  //strings each with its own = characters at the end. So here we will split on the first = only.
  //boost::split(splitContentType, contentType, [](char a){return a=='=';}, boost::token_compress_on);
  {
    const auto i = contentType.find_first_of('=');
    if (std::string::npos != i)
    {
      splitContentType.push_back(contentType.substr(0, i));
      splitContentType.push_back(contentType.substr(i+1));
    }
  }

  if(splitContentType.size() == 2)
  {
    std::string boundary = "--" + splitContentType.at(1);
    boundary.erase(std::remove_if(boundary.begin(), boundary.end(), [](int c){return c=='"';}), boundary.end());

    std::vector<std::string> parts;
    tpSplit(parts, content, boundary);

    size_t iMax = parts.size()-1;
    for(size_t i=1; i<iMax; i++)
    {
      std::string& part = parts.at(i);
      if(part.size()>3)
      {
        boost::string_view str(part.data()+2, part.size()-4);
        tp_www::MultipartFormData param;
        parseMultipartParam(err, str, param);
        params[param.name] = param;
      }
    }

    return true;
  }

  return true;
}

}

//##################################################################################################
Server::Server(tp_www::Route* root):
  m_root(root)
{

}

//##################################################################################################
void Server::exec(int threadCount)
{
  FCGX_Init();

  std::vector<std::thread*> threads;

  for(int i=0; i<threadCount; i++)
  {
    threads.push_back(new std::thread([this]()
    {
      FCGX_Request fcgiRequest;
      FCGX_InitRequest(&fcgiRequest, 0, 0);

      while(FCGX_Accept_r(&fcgiRequest) == 0)
      {
        bool        error        = false;
        int         errorCode    = 200;
        std::string errorMessage = "";

        fcgi_streambuf cin_fcgi_streambuf(fcgiRequest.in);
        fcgi_streambuf cout_fcgi_streambuf(fcgiRequest.out);
        fcgi_streambuf cerr_fcgi_streambuf(fcgiRequest.err);

        std::istream fcgiCin ( &cin_fcgi_streambuf);
        std::ostream fcgiCout(&cout_fcgi_streambuf);
        std::ostream fcgiCerr(&cerr_fcgi_streambuf);

        tp_www::RequestType requestType = tp_www::RequestType::GET;
        if(!error)
        {
          const char* REQUEST_METHOD = FCGX_GetParam("REQUEST_METHOD", fcgiRequest.envp);
          if(REQUEST_METHOD)
            requestType = tp_www::requestTypeFromString(REQUEST_METHOD);
        }

        std::vector<std::string> route;
        std::unordered_map<std::string, std::string> getParams;
        if(!error)
        {
          const char* REQUEST_URI = FCGX_GetParam("REQUEST_URI", fcgiRequest.envp);
          if(REQUEST_URI)
          {
            std::vector<std::string> splitURI;
            boost::split(splitURI, REQUEST_URI, [](char a){return a=='?';}, boost::token_compress_on);

            if(!splitURI.empty())
            {
              boost::split(route, splitURI.at(0), [](char a){return a=='/';}, boost::token_compress_on);
              route.erase(std::remove(route.begin(), route.end(), ""), route.end());

              if(splitURI.size() == 2)
                splitParams(splitURI.at(1), getParams);
            }
          }
        }

#if 0
        //Print out the env
        if(route.size()>0 && route.at(route.size()-1) == "add")
        {
          char **envp = fcgiRequest.envp;
          std::string env;
          for(; *envp; envp++)
            env += *envp + std::string("\n");
          tp_utils::writeBinaryFile("/home/tom/Desktop/env.dat", env);
        }
#endif

        std::unordered_map<std::string, std::string> postParams;
        std::unordered_map<std::string, tp_www::MultipartFormData> multipartFormData;
        if(!error)
        {
          if (requestType == tp_www::RequestType::POST)
          {
            size_t contentLength = size_t(tpMax(0, atoi(FCGX_GetParam("CONTENT_LENGTH", fcgiRequest.envp))));

            if(contentLength>0)
            {
              const char* CONTENT_TYPE = FCGX_GetParam("CONTENT_TYPE", fcgiRequest.envp);
              std::string contentType;
              if(CONTENT_TYPE)
                contentType = CONTENT_TYPE;

              std::string content;
              content.resize(contentLength);
              fcgiCin.read(&content[0], std::streamsize(contentLength));

              if((fcgiCin.rdstate() & std::ifstream::failbit) == 0)
              {
                //-- Multipart form data -----------------------------------------------------------
                static const std::string multipart("multipart/form-data;");
                if(tpStartsWith(contentType, multipart))
                {
                  if(!splitMultipartParams(fcgiCerr, content, contentType, multipartFormData))
                  {
                    error        = true;
                    errorCode    = 400;
                    errorMessage = "Failed to parse multipart/form-data.";
                  }
                }

                //-- Conventional post params ------------------------------------------------------
                else
                {
                  splitParams(content, postParams);
                }
              }
              else
              {
                error        = true;
                errorCode    = 400;
                errorMessage = "Failed to read content.";
              }
            }
          }

          const char* QUERY_STRING = FCGX_GetParam("QUERY_STRING", fcgiRequest.envp);
          if(QUERY_STRING)
          {
            fcgiCerr << QUERY_STRING;

          }
        }

        tp_www::Request request(fcgiCout,
                                fcgiCerr,
                                route,
                                requestType,
                                postParams,
                                getParams,
                                multipartFormData);

        if(error)
        {
          request.sendHeader(errorCode, "text/html");
          request.out() << errorMessage;
        }
        else if(!m_root->handleRequest(request, 0))
        {
          request.sendHeader(404, "text/html");
          request.out() << "Page Not Found 404";
        }
      }
    }));
  }

  for(std::thread* t : threads)
  {
    t->join();
    delete t;
  }
}
}
