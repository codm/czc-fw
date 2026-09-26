"""Keep the pinned HTTP server responsive across idle and interrupted clients."""
from pathlib import Path
import re

MARKER = '// CZC: idle HTTP sockets must not block ready UI/API requests.'

def patch(sdk):
    directory = Path(sdk) / 'libraries/WebServer/src'
    header = directory / 'WebServer.h'
    source = directory / 'WebServer.cpp'
    h, s = header.read_text(), source.read_text()
    patch_multipart(directory / 'Parsing.cpp')
    if MARKER in h and MARKER in s:
        return
    assert MARKER not in h and MARKER not in s, 'Incomplete WebServer patch'
    field = '  WiFiClient  _currentClient;'
    accept = '    _currentClient = _server.available();'
    close = 'void WebServer::close() {\n  _server.close();'
    assert h.count(field)==1 and s.count(accept)==1 and s.count(close)==1, 'Unexpected WebServer SDK source'
    h = h.replace(field, field + '\n  ' + MARKER + '''
  struct IdleHttpClient { WiFiClient client; unsigned long since=0; } _idleHttp[3];''')
    s = s.replace(accept, '    ' + MARKER + '''
    for(auto &p: _idleHttp) {
      if(!p.client.connected()) { p.client=WiFiClient(); continue; }
      if(!_currentClient && p.client.available()) {
        _currentClient=p.client; p.client=WiFiClient();
      } else if(millis()-p.since>HTTP_MAX_DATA_WAIT) p.client.stop();
    }
    if(!_currentClient) {
      WiFiClient next=_server.available();
      if(next && next.available()) _currentClient=next;
      else if(next) {
        for(auto &p: _idleHttp) if(!p.client.connected()) {
          p.client=next; p.since=millis(); break;
        }
      }
    }''')
    s = s.replace(close, close + '\n  for(auto &p: _idleHttp) p.client.stop();')
    header.write_text(h)
    source.write_text(s)


def patch_multipart(source):
    marker = '// CZC: incomplete multipart lines must abort the upload.'
    s = source.read_text()
    if marker in s:
        return
    start = s.index('bool WebServer::_parseForm(')
    end = s.index('\nString WebServer::urlDecode(', start)
    form = s[start:end]
    anchor = '  String line;\n'
    assert form.count(anchor) == 1, 'Unexpected multipart parser'
    form = form.replace(anchor, anchor + '  ' + marker + '''
  auto aborted = [&]() {
    return _currentUpload ? _parseFormUploadAborted() : false;
  };
  auto readLine = [&](String &value) {
    value = "";
    unsigned long started = millis();
    while (millis() - started < client.getTimeout()) {
      int c = _uploadReadByte(client);
      if (c < 0) return false;
      if (c == '\\r') return _uploadReadByte(client) == '\\n';
      if (value.length() >= 1024 || !value.concat((char)c)) return false;
    }
    return false;
  };
''')
    first = '''  do {
    line = client.readStringUntil('\\r');
    ++retry;
  } while (line.length() == 0 && retry < 3);

  client.readStringUntil('\\n');'''
    assert first in form
    form = form.replace(first, '''  do {
    if (!readLine(line)) return aborted();
    ++retry;
  } while (line.length() == 0 && retry < 3);''')
    pair = r"client\.readStringUntil\('\\r'\);\s*client\.readStringUntil\('\\n'\);"
    form, lines = re.subn(r'line = ' + pair, 'if (!readLine(line)) return aborted();', form)
    # The empty line after Content-Type must also arrive completely.
    form, blanks = re.subn(pair, 'if (!readLine(line)) return aborted();', form)
    assert lines == 3 and blanks == 1, 'Unexpected multipart line reads'
    old_tail = '''                line = client.readStringUntil(0x0D);
                client.readStringUntil(0x0A);'''
    assert old_tail in form
    form = form.replace(old_tail, '')
    end_file = '                _currentUpload->status = UPLOAD_FILE_END;'
    assert form.count(end_file) == 1
    form = form.replace(end_file, '''                // Validate the boundary suffix before announcing file completion.
                if (!readLine(line) || (line != "" && line != "--")) return aborted();
''' + end_file)
    form = form.replace('strstr((const char*)endBuf, boundary.c_str()) != NULL',
                        'memcmp(endBuf, boundary.c_str(), boundary.length()) == 0')
    form = form.replace('              return false;', '              return aborted();')
    form = form.replace('  return false;\n}', '  return aborted();\n}')
    assert 'readStringUntil' not in form
    source.write_text(s[:start] + form + s[end:])

if __name__ == '__main__':
    import sys
    patch(sys.argv[1])
