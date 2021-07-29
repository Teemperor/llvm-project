import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

import json

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @no_debug_info_test
    def test_mixed_log_formats(self):
        """
        Tests having two log channels that target the same output file but with
        different output formats (JSON and plain text).
        """
        self.build()
        logfile = self.getBuildArtifact("mixed.log")

        # Enable two logging channels. One uses JSON, the other plain text
        # but they log to the same file. Timestamps are enabled so that
        # the start of the plain text messages are just numbers and not by
        # accident something that looks like a JSON object ("{...}").
        self.expect("log enable -T -O json lldb target comm -f {}".format(logfile))
        self.expect("log enable -T -O plain gdb-remote all -f {}".format(logfile))

        lldbutil.run_to_source_breakpoint(self, "// break here", lldb.SBFileSpec("main.cpp"))

        if configuration.is_reproducer_replay():
            logfile = self.getReproducerRemappedPath(logfile)

        self.assertTrue(os.path.isfile(logfile))
        with open(logfile, 'r') as f:
            log_lines = f.readlines()
        log_content = "\n".join(log_lines)

        # Go over the log and try to parse all the JSON messages. Even with
        # the also enabled plain text logging every separate JSON message should
        # be valid.
        found_json = False
        for line in log_lines:
            line = line.strip()
            if line.startswith("{"):
                found_json = True
                json_payload = line
                # Strip the trailing comma that is behind every object in the
                # big JSON array that the logger is printing. JSON can't have
                # trailing commas behind the top-level object, so we have to
                # to make the JSON parser happy by removing the comma.
                self.assertTrue(json_payload.endswith(","), "payload: " + json_payload)
                json_payload = json_payload[:-1]
                parsed_payload = json.loads(json_payload)
                # Check that message and timestamps were added to the log
                # message.
                self.assertIn("timestamp", parsed_payload)
                self.assertIn("message", parsed_payload)

        # Sanity check that we got at least one JSON log message.
        self.assertTrue(found_json, "log: " + log_content)

        # Go over the log and try to verify the plain text messages.
        found_plain = False
        for line in log_lines:
            line = line.strip()
            # The only thing that can be checked for plain text is that there
            # is at least one line that starts with something that looks like
            # a timestamp (i.e., a digit).
            if not line.startswith("{") and len(line) != 0 and line[0].isdigit():
                found_plain = True
                break

        # Sanity check that we got a tleast one plain text message.
        self.assertTrue(found_plain, "log: " + log_content)
