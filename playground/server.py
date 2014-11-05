#!/usr/bin/python

##
## Copyright (C) 2014 SCALITY SA - http://www.scality.com
##
## This file is part of ScalityRestBlock.
##
## ScalityRestBlock is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## ScalityRestBlock is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with ScalityRestBlock.  If not, see <http://www.gnu.org/licenses/>.
##
##


import sys
import errno
import os
import argparse
import time
import BaseHTTPServer
import SocketServer
import threading

class RestHandler(BaseHTTPServer.BaseHTTPRequestHandler):

    protocol_version='HTTP/1.1'

    datapath=None

    @classmethod
    def setDatapath(cls, path):
        cls.datapath=path

    def _error(self, code, msg=''):
        print("[Error]: %s -> %s" % (code, msg))
        self.send_response(code)
        self.send_header('Content-Length', len(msg))
        self.end_headers()
        if len(msg):
            self.wfile.write(msg)

    def _get_fpath(self):
        partitions = self.path.rpartition('?')
        uri = partitions[2]
        if partitions[1] == '?':
            uri = partitions[0]
        if uri[0] == '/':
            uri = uri[1:]
        fpath = os.path.join(self.datapath, uri)
        return fpath

    def _get_range(self, path):
        datarange = [0, -1]
        hrange = self.headers.get("Range")
        size = 0
        try:
            info = os.stat(path)
            size = info.st_size
        except OSError as e:
            if e.errno != errno.ENOENT:
                self._error(500, "Internal error: Could not compute range: %s" % (str(e)))

        offset = 0
        if hrange != None:
            # span the "bytes="
            hrange = hrange.split('=')[1]

            hrange = hrange.split('-') 
            if len(hrange) != 2:
                self._error(500, "Internal error: Bad Range")
                return None, None, None
            datarange = [ int(hrange[0]), int(hrange[1]) ]
            rsize = datarange[1] - datarange[0] + 1
            if rsize >= 0 and rsize <= size and datarange[1] <= size:
                offset = datarange[0]
                size = rsize
            else:
                self._error(500, "Internal error: Bad range (Size invalid: %i+%i=%i/%i)"
                            % (datarange[0], rsize, datarange[0]+rsize, size))
                return None, None, None
        return datarange, size, offset

    def _read_metadata(self, fpath, fsize):
        data = """{
  "metadata": {
    "cdmi_size": """
        data += str(fsize)
        data += """,
    "scal_ino": "542",
    "scal_uid": "1",
    "scal_gid": "1",
    "scal_perms": "420",
    "scal_atime": "0",
    "scal_ctime": "0",
    "scal_mtime": "0",
    "scal_nlink": "1",
    "cdmi_mtime": "1970-01-01T00:00:01.000000Z",
    "cdmi_atime": "1970-01-01T00:00:01.000000Z"
  }
}
"""
        self.send_response(200)
        self.send_header("Content-Length", len(data))
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(data)

    def _read_file(self, path, offset, size):
        failure = True
        rbuf = None
        try:
            with open(path, 'r+b') as f:
                f.seek(offset)
                rbuf = f.read(size)
            failure = False
        except OSError as e:
            if e.errno == errno.ENOENT:
                self._error(404, "File not found")
            else:
                self._error(500, "Internal error: %s" % (str(e)))
        except Exception as e:
            self._error(500, "Internal error: %s" % (str(e)))
        finally:
            if failure:
                return
            
        self.send_response(200)
        self.send_header("Content-Type", "application/binary")
        self.send_header("Content-Length", len(rbuf))
        self.end_headers()
        self.wfile.write(rbuf)

    def _truncate_file(self, path, size):
        if not os.path.exists(path):
            self._error(404, "Cannot truncate non-existing file")
            return

        failure = True
        error_sent = False
        try:
            # Ensure both files are created at the same time.
            with open(path, 'w') as f:
                f.truncate(size)
            failure = False
        except OSError as e:
            if e.errno == errno.EEXIST:
                self._error(412, "File %s already exists" % (path))
            self._error(500, "Internal error: Could not truncate: %s" % (str(e)))
            error_sent = True
        except Exception as e:
            self._error(500, "Internal error: Could not truncate: %s" % (str(e)))
            error_sent = True
        finally:
            if failure:
                if error_sent is False:
                    self._error(500, "Internal error: Unknown error while Truncating...")
                return
            
        self.send_response(200)
        self.send_header("Content-Length", 0)
        self.end_headers()

    def _write_file(self, path, offset, data, creat):
        flags = 'w+b'
        if os.path.exists(path):
            if creat:
                self._error(412, "Cannot Exclusively create file: already exists")
                return
            flags = 'r+b'

        failure = True
        error_sent = False
        try:
            # Ensure both files are created at the same time.
            with open(path, flags) as f:
                f.seek(offset)
                if data is not None and len(data) != 0:
                    f.write(data)
            failure = False
        except OSError as e:
            if e.errno == errno.ENOENT:
                self._error(404, "File not found")
            elif e.errno == errno.EEXIST:
                self._error(412, "File %s already exists" % (path));
            else:
                self._error(500, "Internal error: Could not write: %s" % (str(e)))
            error_sent = True
        finally:
            if failure is True:
                if error_sent is False:
                    self._error(500, "Internal error: Unknown error while writing...")
                return
            
        self.send_response(204)
        self.end_headers()

    def _list_dir(self, path, do_json):
        data = ""
        if not path.endswith('/'):
            self._error(301, "Moved permanently")
            return
        entries = os.listdir(path)
        if do_json:
            data = """{
  "capabilitiesURI": "/cdmi_capabilities/container/",
  "objectName": "/",
  "parentID": "00009271001C56EEC50A800000000000000001000000000200000100",
  "parentURI": "/",
  "objectID": "00009271001C56EEC50A800000000000000001000000000200000100",
  "metadata": {
    "scal_ino": "1",
    "scal_uid": "1",
    "scal_gid": "1",
    "scal_perms": "493",
    "scal_atime": "0",
    "scal_ctime": "0",
    "scal_mtime": "0",
    "scal_nlink": "2",
    "cdmi_mtime": "1970-01-01T00:00:01.000000Z",
    "cdmi_atime": "1970-01-01T00:00:01.000000Z"
  },
  "objectType": "application/cdmi-container",
  "children": ["""

        else:
            data = "<html><head><title>Listing of the volumes</title></head><body><li>"
        for entry in entries:
            if do_json:
                data += "\"%s\"," % (str(entry))
            else:
                data += "<ul>%s</ul>" % (str(entry))
        if do_json:
                data += """],
  "childrenrange": ""
}"""
        else:
            data += "</li></body></html>"
        self.send_response(200)
        if do_json:
            self.send_header("Content-Type", "application/cdmi-container")
        else:
            self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", len(data))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        """Respond to a GET request."""
        # Check for:
        # Url: ?metadata   <- MD GET
        # Range: bytes=N-M   <- Range
        # X-CDMI-Specification-Version: 1.0.1 <- Get Json listing for directory
        fpath = self._get_fpath()
        if os.path.isdir(fpath):
            jsonlist = 1
            if self.headers.get("X-CDMI-Specification-Version") != "1.0.1":
                jsonlist = 0
            try:
                self._list_dir(fpath, jsonlist)
            except Exception as e:
                self._error(500, "Internal error: %s" % (str(e)))
        else:
            if self.path.endswith('?metadata'):
                info = None
                try:
                    info = os.stat(fpath)
                except:
                    self._error(500, "Internal error: Cannot stat file %s" % (fpath))
                    return
                self._read_metadata(fpath, info.st_size)
            else:
                datarange, size, offset = self._get_range(fpath)
                if datarange is None and size is None and offset is None:
                    return
                self._read_file(fpath, offset, size)
                
    def do_PUT(self):
        # Check for:
        # If-None-Match: <- Exclusive put (CREATE)
        # X-Scal-Truncate: <- Truncate size
        # Range: bytes=N-M   <- Range
        fpath = self._get_fpath()
        # Only support File PUTs
        if self.path.endswith('?metadata'):
            self._error(500, "Internal error: PUT not supported for METADATA")
            return
        creat_excl = False
        if self.headers.get("If-None-Match") != None:
            creat_excl = True
        datarange, size, offset = self._get_range(fpath)
        if datarange is None and size is None and offset is None:
            return
        if self.headers.get("X-Scal-Truncate") is None:
            # Means CREATE or WRITE
            data = None
            if (self.headers.get("Content-Length") is not None and
                int(self.headers.get("Content-Length")) != 0):
                data = self.rfile.read(int(self.headers.get("Content-Length")))
            self._write_file(fpath, offset, data, creat_excl)
        else:
            self._truncate_file(fpath, int(self.headers.get("X-Scal-Truncate")))

    def do_DELETE(self):
        fpath = self._get_fpath()
        try:
            os.remove(fpath)
        except OSError as e:
            if e.errno == errno.ENOENT:
                self._error(404, "File not found")
            else:
                self._error(500, "Could not delete volume: %s" % (str(e)))
            return
        except Exception as e:
            self._error(500, "Could not delete volume: %s" % (str(e)))
            return
        self.send_response(204)
 

class ThreadedHTTPServer(SocketServer.ThreadingMixIn, BaseHTTPServer.HTTPServer):
    """Handle requests in separate threads"""
    daemon_threads = True

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Command-Line options to'
                                     ' start the playground REST server.')
    parser.add_argument('--port', dest='port', type=int, default=80,
                        help='The port to listen from (default: 8080)')
    parser.add_argument('--datapath', dest='datapath', default='./playground_data/',
                        help='The directory to store the data into '
                        '(default: ./playground_data/')

    args = parser.parse_args()
    if os.path.isdir(args.datapath) is not True:
        os.mkdir(args.datapath)
    RestHandler.setDatapath(args.datapath)

    httpd = ThreadedHTTPServer(('0.0.0.0', args.port), RestHandler)
    print time.asctime(), "Server Starts - %s:%s" % ("0.0.0.0", args.port)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    httpd.server_close()
    print time.asctime(), "Server Stops - %s:%s" % ("0.0.0.0", args.port)
