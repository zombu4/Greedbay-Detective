#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace app::browserprotocol {

// The app and its browser worker (a separate process that owns the Qt
// WebEngine browser for one session) talk over a local socket. Every message
// is one JSON object on one line, UTF-8, ending in "\n".
//
// Worker -> app, once after connecting:
//   {"event":"ready","token":"<token from the command line>"}
// App -> worker, one request at a time:
//   {"id":1,"cmd":"fetch","kind":"activeSearch|soldSearch|item|research","url":"https://..."}
//   {"id":2,"cmd":"signIn"}
//   {"id":3,"cmd":"quit"}
// Worker -> app, one reply per request, with the request's id:
//   {"id":1,"status":"ok","result":{...extractor result...}}
//   {"id":1,"status":"unexpected_page","page":"error_page|sign_in|no_access|unexpected","reason":"..."}
//   {"id":2,"status":"ok","signedIn":true}
//   {"id":2,"status":"window_closed","signedIn":false}
//   {"id":n,"status":"failed","error":"..."}
//   {"id":3,"status":"ok"}   (sent after the browser is torn down; the worker then exits)
// When the socket closes, the worker tears the browser down and exits.

inline constexpr char kEventReady[] = "ready";

inline constexpr char kCmdFetch[] = "fetch";
inline constexpr char kCmdSignIn[] = "signIn";
inline constexpr char kCmdQuit[] = "quit";

inline constexpr char kStatusOk[] = "ok";
inline constexpr char kStatusUnexpectedPage[] = "unexpected_page";
inline constexpr char kStatusWindowClosed[] = "window_closed";
inline constexpr char kStatusFailed[] = "failed";

// A line longer than this is a protocol error. Extractor results for one page
// of 240 search results are far below it.
inline constexpr qsizetype kMaxLineBytes = 16 * 1024 * 1024;

// Serializes `message` as one protocol line.
QByteArray encode(const QJsonObject &message);

// Splits a byte stream into protocol lines. Feed it whatever the socket
// delivered; take complete messages out with next().
class LineReader {
public:
    void append(const QByteArray &bytes);

    // The next complete message, or std::nullopt when no complete line is
    // buffered. Sets *error and returns std::nullopt when a line is not a
    // JSON object or is longer than kMaxLineBytes; the reader is then broken
    // and the connection must be closed.
    std::optional<QJsonObject> next(QString *error);

private:
    QByteArray m_buffer;
    bool m_broken = false;
};

} // namespace app::browserprotocol
