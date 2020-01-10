""" Helper functions to communicate with replication servers.
derived from https://github.com/osmcode/pyosmium
"""

import sys
import urllib.request as urlrequest
import urllib.error as urlerror
import datetime as dt
from collections import namedtuple
from math import ceil

OsmosisState = namedtuple('OsmosisState', ['sequence', 'timestamp'])
DownloadResult = namedtuple('DownloadResult', ['id', 'newest'])

import logging

log = logging.getLogger()

class ReplicationServer(object):
    def __init__(self, url, diff_type='osc.gz'):
        self.baseurl = url
        self.diff_type = diff_type

    def open_url(self, url):
        return urlrequest.urlopen(url,None,10)

    def timestamp_to_sequence(self, timestamp, balanced_search=False):
        """ Get the sequence number of the replication file that contains the
            given timestamp. The search algorithm is optimised for replication
            servers that publish updates in regular intervals. For servers
            with irregular change file publication dates 'balanced_search`
            should be set to true so that a standard binary search for the
            sequence will be used. The default is good for all known
            OSM replication services.
        """

        # get the current timestamp from the server
        upper = self.get_state_info()

        if upper is None:
            return None
        if timestamp >= upper.timestamp or upper.sequence <= 0:
            return upper.sequence

        # find a state file that is before the required timestamp
        lower = None
        lowerid = 0
        while lower is None:
            log.info("Trying with Id %s" % lowerid)
            lower = self.get_state_info(lowerid)

            if lower is not None and lower.timestamp >= timestamp:
                if lower.sequence == 0 or lower.sequence + 1 >= upper.sequence:
                    return lower.sequence
                upper = lower
                lower = None
                lowerid = 0

            if lower is None:
                # no lower yet, so try a higher id (binary search wise)
                newid = int((lowerid + upper.sequence) / 2)
                if newid <= lowerid:
                    # nothing suitable found, so upper is probably the best we can do
                    return upper.sequence
                lowerid = newid

        # Now do a binary search between upper and lower.
        # We could be clever here and compute the most likely state file
        # by interpolating over the timestamps but that creates a whole ton of
        # special cases that need to be handled correctly.
        while True:
            if balanced_search:
                base_splitid = int((lower.sequence + upper.sequence) / 2)
            else:
                ts_int = (upper.timestamp - lower.timestamp).total_seconds()
                seq_int = upper.sequence - lower.sequence
                goal = (timestamp - lower.timestamp).total_seconds()
                base_splitid = lower.sequence + ceil(goal * seq_int / ts_int)
                if base_splitid >= upper.sequence:
                    base_splitid = upper.sequence - 1
            split = self.get_state_info(base_splitid)

            if split is None:
                # file missing, search the next towards lower
                splitid = base_splitid - 1
                while split is None and splitid > lower.sequence:
                    split = self.get_state_info(splitid)
                    splitid -= 1
            if split is None:
                # still nothing? search towards upper
                splitid = base_splitid + 1
                while split is None and splitid < upper.sequence:
                    split = self.get_state_info(splitid)
                    splitid += 1
            if split is None:
                # still nothing? Then lower has to do
                return lower.sequence

            # set new boundary
            if split.timestamp < timestamp:
                lower = split
            else:
                upper = split

            if lower.sequence + 1 >= upper.sequence:
                return lower.sequence


    def get_state_info(self, seq=None):
        """ Downloads and returns the state information for the given
            sequence. If the download is successful, a namedtuple with
            `sequence` and `timestamp` is returned, otherwise the function
            returns `None`.
        """
        try:
            response = self.open_url(self.get_state_url(seq))
        except Exception as err:
            logging.error(err)
            return None

        ts = None
        seq = None
        line = response.readline()
        while line:
            line = line.decode('utf-8')
            if '#' in line:
                line = line[0:line.index('#')]
            else:
                line = line.strip()
            if line:
                kv = line.split('=', 2)
                if len(kv) != 2:
                    return None
                if kv[0] == 'sequenceNumber':
                    seq = int(kv[1])
                elif kv[0] == 'timestamp':
                    ts = dt.datetime.strptime(kv[1], "%Y-%m-%dT%H\\:%M\\:%SZ")
                    if sys.version_info >= (3,0):
                        ts = ts.replace(tzinfo=dt.timezone.utc)
            line = response.readline()

        return OsmosisState(sequence=seq, timestamp=ts)

    def get_diff_block(self, seq):
        """ Downloads the diff with the given sequence number and returns
            it as a byte sequence. Throws a :code:`urllib.error.HTTPError`
            (or :code:`urllib2.HTTPError` in python2)
            if the file cannot be downloaded.
        """
        return self.open_url(self.get_diff_url(seq)).read()


    def get_state_url(self, seq):
        """ Returns the URL of the state.txt files for a given sequence id.

            If seq is `None` the URL for the latest state info is returned,
            i.e. the state file in the root directory of the replication
            service.
        """
        if seq is None:
            return self.baseurl + '/state.txt'

        return '%s/%03i/%03i/%03i.state.txt' % (self.baseurl,
                     seq / 1000000, (seq % 1000000) / 1000, seq % 1000)


    def get_diff_url(self, seq):
        """ Returns the URL to the diff file for the given sequence id.
        """
        return '%s/%03i/%03i/%03i.%s' % (self.baseurl,
                     seq / 1000000, (seq % 1000000) / 1000, seq % 1000,
                     self.diff_type)

