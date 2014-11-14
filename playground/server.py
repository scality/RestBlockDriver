# Copyright (C) 2014 SCALITY SA - http://www.scality.com
#
# This file is part of ScalityRestBlock.
#
# ScalityRestBlock is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# ScalityRestBlock is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with ScalityRestBlock.  If not, see <http://www.gnu.org/licenses/>.

import argparse
import errno
import logging
import os

try:
    import chaussette.util
except ImportError:
    chaussette = None

import falcon

# Create & configure root logger
LOGGER = logging.getLogger(__name__)
if chaussette:
    chaussette.util.configure_logger(LOGGER, level='DEBUG')

def ensure_exists(func):
    """
        This is a decorator that raises
        the proper HTTP error if the volume does not exist
    """
    def wrapper(*args, **kwargs):
        """ finale callable part """
        vol = args[0]
        if not vol.exists():
            raise falcon.HTTPNotFound()
        return func(*args, **kwargs)
    return wrapper

class Volume(object):
    """
        This class represents a volume
        and the operations that can be done on it
    """

    def __init__(self, fpath):
        self._path = fpath
        self._logger = LOGGER.getChild('Volume')
        self._stat = None
        try:
            self._stat = os.stat(fpath)
        except OSError:
            pass
        if os.path.isdir(fpath):
            raise falcon.HTTPBadRequest('Bad request',
                                        'Resource is a directory')

    def exists(self):
        """ Existence check facility for the volume """
        return self._stat != None

    def read_md(self):
        """ Read facility for the Volume """
        data = """{
  "metadata": {
    "cdmi_size": """
        data += str(self._stat.st_size)
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
        return data

    @ensure_exists
    def read(self, offset, size):
        """ Read facility for the Volume """

        if offset + size > self._stat.st_size:
            raise falcon.HTTPBadRequest(
                "Data Range is invalid",
                'Data requested goes further than the file size.')
        if offset is None or size is None:
            raise falcon.HTTPBadRequest(
                "Data Range is invalid",
                'Computed offset and sizes are invalid.')

        data = None
        try:
            with open(self._path, 'r+b') as openfile:
                openfile.seek(offset)
                data = openfile.read(size)
        except OSError as ex:
            if ex.errno == errno.ENOENT:
                raise falcon.HTTPNotFound()
            else:
                raise falcon.HTTPInternalServerError(
                    'Internal Server Error', str(ex))
        return data

    @ensure_exists
    def write(self, offset, data):
        """ Write facility for the Volume """

        try:
            # Ensure both files are created at the same time.
            with open(self._path, 'r+b') as openfile:
                openfile.seek(offset)
                if data is not None and len(data) != 0:
                    openfile.write(data)
        except OSError as ex:
            if ex.errno == errno.ENOENT:
                raise falcon.HTTPInternalServerError(
                    'Internal Server Error',
                    "File not found when expected to find it.")
            else:
                raise falcon.HTTPInternalServerError(
                    'Internal Server Error',
                    "Could not write: %s" % (str(ex)))

    @ensure_exists
    def truncate(self, size):
        """ Truncate facility for the Volume """
        try:
            with open(self._path, 'r+b') as openfile:
                openfile.truncate(size)
        except OSError as ex:
            if ex.errno == errno.ENOENT:
                raise falcon.HTTPInternalServerError(
                    'Internal Server Error',
                    "File not found when expected to find it.")
            raise falcon.HTTPInternalServerError(
                'Internal Server Error',
                "Could not truncate: %s" % (str(ex)))

    def create(self):
        """ Create facility for the Volume """
        if self.exists():
            raise falcon.HTTPPreconditionFailed(
                'Precondition Failed',
                'Cannot create volume: volume exists')

        try:
            with open(self._path, 'w'):
                pass
        except OSError as ex:
            raise falcon.HTTPInternalServerError(
                'Internal Server Error',
                'Cannot create volume: %s' % (str(ex)))

    @ensure_exists
    def destroy(self):
        """ Destroy facility for the Volume """
        try:
            os.remove(self._path)
        except OSError as ex:
            if ex.errno == errno.ENOENT:
                raise falcon.HTTPInternalServerError(
                    'Internal Server Error',
                    "File not found when expected to find it.")
            else:
                raise falcon.HTTPInternalServerError(
                    'Internal Server Error',
                    "Could not delete volume: %s" % (str(ex)))

class VolumeHandler(object):
    """
        Request Handler for requests on the CDMI volumes
    """

    def __init__(self, datapath):
        self._datapath = datapath
        self._logger = LOGGER.getChild('VolumeHandler')

    def _get_volume(self, resource):
        partitions = resource.rpartition('?')
        uri = partitions[2]
        if partitions[1] == '?':
            uri = partitions[0]
        if uri[0] == '/':
            uri = uri[1:]
        fpath = os.path.join(self._datapath, uri)
        return Volume(fpath)

    def _get_range(self, request):
        datarange = None
        try:
            datarange = request.range
        except falcon.request.InvalidHeaderValueError:
            hdr = request.env['HTTP_RANGE']
            if hdr.startswith('bytes='):
                request.env['HTTP_RANGE'] = hdr[6:]
            datarange = request.range

        size = 0
        offset = 0
        if datarange != None:
            size = datarange[1] - datarange[0] + 1
            offset = datarange[0]
            if size < 0:
                raise falcon.HTTPInternalServerError(
                    'Internal Server Error',
                    "Bad range (Negative Size computed from range %i-%i=%i)"
                    % (datarange[0], datarange[1], size))
        return datarange, size, offset

    def _read_metadata(self, response, volume):
        response.status = falcon.HTTP_200
        response.content_type = "application/json"
        response.body = volume.read_md()

    def _read_file(self, response, volume, offset, size):
        response.status = falcon.HTTP_200
        response.content_type = "application/binary"
        response.body = volume.read(offset, size)

    def _create_file(self, response, volume):
        volume.create()
        response.status = falcon.HTTP_204

    def _truncate_file(self, response, volume, size):
        volume.truncate(size)
        response.status = falcon.HTTP_200

    def _write_file(self, response, volume, offset, data):
        volume.write(offset, data)
        response.status = falcon.HTTP_204

    def on_get(self, request, response, volname):
        """ VolumeHandler's GET handler """
        self._logger.debug("[VolumeHandler] GET %s" % (volname))
        volume = self._get_volume(volname)
        # Note: falcon won't see a parameter with no value as a parameter,
        # so we need to check in the uri to find out if the metadata is
        # what the user wants to read
        if '?metadata' in request.uri:
            self._read_metadata(response, volume)
        else:
            datarange, size, offset = self._get_range(request)
            self._read_file(response, volume, offset, size)

    def on_put(self, request, response, volname):
        """ VolumeHandler's PUT handler """
        # Check for:
        # If-None-Match: <- Exclusive put (CREATE)
        # X-Scal-Truncate: <- Truncate size
        # Range: bytes=N-M   <- Range
        volume = self._get_volume(volname)
        if request.get_param('metadata'):
            raise falcon.HTTPInternalServerError(
                'Internal Server Error',
                "PUT not supported for METADATA")

        datarange, size, offset = self._get_range(request)
        self._logger.debug("[VolumeHandler] PUT %s (%s-%s)" % (volname, str(size), str(offset)))

        # Check for create
        if request.if_none_match is not None:
            self._create_file(response, volume)

        # Then PUT (might be a write or a truncate)
        truncsz = request.get_header("X-Scal-Truncate")
        if truncsz is not None:
            try:
                truncsz = int(truncsz)
            except Exception:
                raise falcon.HTTPInternalServerError(
                    'Internal server error',
                    'Could not translate Trunc size to integer.')
            self._truncate_file(response, volume, truncsz)

        elif request.if_none_match is None:
            if request.content_length is None:
                raise falcon.HTTPLengthRequired(
                    'Length Required',
                    'Cannot write data without length')
            data = request.stream.read(request.content_length)
            self._write_file(response, volume, offset, data)

    def on_delete(self, request, response, volname):
        """ VolumeHandler's DELETE handler """
        volume = self._get_volume(volname)
        volume.destroy()
        response.status = falcon.HTTP_204

class Root(object):
    """
        Request Handler for requests on the CDMI directory
    """

    def __init__(self, datapath):
        self._datapath = datapath
        self._logger = LOGGER.getChild('Root')

    def _get_fpath(self, resource):
        partitions = resource.rpartition('?')
        uri = partitions[2]
        if partitions[1] == '?':
            uri = partitions[0]
        if uri[0] == '/':
            uri = uri[1:]
        fpath = os.path.join(self._datapath, uri)
        return fpath

    def _list_dir(self, response, path, do_json):
        data = ""
        if not path.endswith('/'):
            response.status = falcon.HTTP_301
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
            data = "<html><head><title>Listing of the volumes"
            data += "</title></head><body><li>"
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
        response.status = falcon.HTTP_200
        if do_json:
            response.content_type = "application/cdmi-container"
        else:
            response.content_type = "text/html"
        response.body = data

    def on_get(self, request, response):
        """Respond to a GET request."""
        # Check for:
        # Url: ?metadata   <- MD GET
        # Range: bytes=N-M   <- Range
        # X-CDMI-Specification-Version: 1.0.1 <- Get Json listing for directory

        fpath = self._get_fpath(request.path)
        self._logger.info('[RootHandler] GET %s' % ((request.path)))
        if not os.path.isdir(fpath):
            raise falcon.HTTPBadRequest(
                'Bad Request',
                'Handler does not support GET on files')

        jsonlist = 1
        if request.get_header("X-CDMI-Specification-Version") != "1.0.1":
            jsonlist = 0
        try:
            self._list_dir(response, fpath, jsonlist)
        except OSError as err:
            raise falcon.HTTPInternalServerError(
                'Internal Server Error', str(err))


def sink(req, resp):
    logger = LOGGER.getChild('root')
    logger.warn("Sunk request for %s into the sink." % (req.path))
    resp.status = falcon.HTTP_403
    resp.body = "<html><head><title>Forbidden</title></head><body><h1>Path"\
                "forbidden by server</h1></body></html>"

CatchallHandler = Root

if not os.path.exists("playground_data"):
    os.mkdir("playground_data")

LOGGER.info('Starting app')
app = falcon.API()
app.add_route('/', Root("playground_data"))
app.add_route('/{volname}', VolumeHandler("playground_data"))
app.add_sink(sink)
