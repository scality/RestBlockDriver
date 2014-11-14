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

import logging

try:
    import chaussette.util
except ImportError:
    chaussette = None

import falcon

# Create & configure root logger
LOGGER = logging.getLogger(__name__)
if chaussette:
    chaussette.util.configure_logger(LOGGER, level='DEBUG')

# Current handlers are for demo purpose only

class Root(object):
    def __init__(self):
        self._logger = LOGGER.getChild('root')

    def on_get(self, request, response):
        response.content_type = 'text/plain'
        response.status = falcon.HTTP_200

        self._logger.info('Hello, world!')

        data = ['abc']
        data_len = sum(len(s) for s in data)
        response.stream = data
        response.stream_len = data_len

CatchallHandler = Root

LOGGER.info('Starting app')
app = falcon.API()
app.add_route('/', Root())
app.set_default_route(CatchallHandler())
