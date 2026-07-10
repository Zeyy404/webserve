#ifndef SESSIONMIDDLEWARE_HPP
# define SESSIONMIDDLEWARE_HPP

#include <string>
#include "SessionManager.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "CookieParser.hpp"

// Ties the HTTP layer to session storage: on the way in it resolves the
// SESSID cookie to an existing Session (or mints a fresh one) and attaches it
// to the request; on the way out it emits a Set-Cookie only when the client's
// session id differs from the one now bound to the request.
class SessionMiddleware {

private:
    
    static const std::string _cookieName;

public:

    SessionMiddleware();
    SessionMiddleware(const SessionMiddleware& other);
    ~SessionMiddleware();

    SessionMiddleware& operator=(const SessionMiddleware& other);

    void    processRequest(HttpRequest& request);
    void    processResponse(HttpRequest& request, HttpResponse& response);

};

#endif