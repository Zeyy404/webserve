#ifndef COOKIE_PARSER_HPP
# define COOKIE_PARSER_HPP

#include <map>
#include <string>
#include <sstream>

// Two-way cookie helper: splits an inbound Cookie header into a name->value
// map, and builds outbound Set-Cookie strings. The member fields hold the
// attributes (Path, Max-Age, HttpOnly, SameSite) stamped onto built cookies.
class CookieParser {

private:

    int         _maxAge;
    bool        _httpOnly;
    std::string _sameSite;
    std::string _path;

    std::string trim(const std::string& s);
    
public:
    
    CookieParser();
    CookieParser(const CookieParser& other);
    ~CookieParser();

    CookieParser& operator=(const CookieParser& other);

    std::map<std::string, std::string>   parse(const std::string& headerValue);
    std::string                          build(const std::string& name, const std::string& value);
    std::string                          buildExpired(const std::string& name);

};

#endif