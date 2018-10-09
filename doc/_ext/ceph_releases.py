# https://github.com/sphinx-contrib/documentedlist/blob/master/sphinxcontrib/documentedlist.py 
# https://github.com/sphinx-doc/sphinx/blob/v1.6.3/sphinx/ext/graphviz.py
# https://github.com/thewtex/sphinx-contrib/blob/master/exceltable/sphinxcontrib/exceltable.py
# https://bitbucket.org/prometheus/sphinxcontrib-htsql/src/331a542c29a102eec9f8cba44797e53a49de2a49/sphinxcontrib/htsql.py?at=default&fileviewer=file-view-default
import json
import six
import yaml
import sphinx
import datetime
from docutils.parsers.rst import Directive
from docutils import nodes
from sphinx.util import logging

class CephReleases(Directive):
    has_content = False
    required_arguments = 1
    optional_arguments = 0
    option_spec = {}

    def run(self):
        filename = self.arguments[0]
        document = self.state.document
        env = document.settings.env
        rel_filename, filename = env.relfn2path(filename)
        env.note_dependency(filename)
        try:
            with open(filename, 'r') as fp:
                releases = yaml.load(fp)
                releases = releases["releases"]
        except Exception as e:
            return [document.reporter.warning(
                "Failed to open Ceph releases file {}: {}".format(filename, e),
                line=self.lineno)]

        table = nodes.table()
        tgroup = nodes.tgroup(cols=3)
        table += tgroup

        tgroup.extend(
            nodes.colspec(colwidth=30, colname='c'+str(idx))
            for idx, _ in enumerate(range(3)))

        thead = nodes.thead()
        tgroup += thead
        row_node = nodes.row()
        thead += row_node
        row_node.extend(nodes.entry(h, nodes.paragraph(text=h))
            for h in ["Version", "Release date", "End of life"])

        tbody = nodes.tbody()
        tgroup += tbody

        rows = []
        for code_name, info in six.iteritems(releases):
            trow = nodes.row()

            entry = nodes.entry()
            para = nodes.paragraph(text="{}".format(code_name))
            entry += para
            trow += entry

            entry = nodes.entry()
            para = nodes.paragraph(text="{}".format(
                info.get("released", "--")))
            entry += para
            trow += entry

            entry = nodes.entry()
            para = nodes.paragraph(text="{}".format(
                info.get("target_eol", "--")))
            entry += para
            trow += entry

            rows.append(trow)

        tbody.extend(rows)

        #for row in range(10):
        #    for cell in range(3):
        #        entry = nodes.entry()
        #        para = nodes.paragraph(text="`Mimic`_")
        #        sphinx.util.nodes.nested_parse_with_titles(
        #            self.state, para, entry) 
        #        trow += entry
        #    rows.append(trow)
        #tbody.extend(rows)

        return [table]

class CephTimeline(Directive):
    has_content = False
    required_arguments = 1
    optional_arguments = 0
    option_spec = {}

    def run(self):
        filename = self.arguments[0]
        document = self.state.document
        env = document.settings.env
        rel_filename, filename = env.relfn2path(filename)
        env.note_dependency(filename)
        try:
            with open(filename, 'r') as fp:
                releases = yaml.load(fp)
        except Exception as e:
            return [document.reporter.warning(
                "Failed to open Ceph releases file {}: {}".format(filename, e),
                line=self.lineno)]

        table = nodes.table()
        tgroup = nodes.tgroup(cols=3)
        table += tgroup

        tgroup.extend(
            nodes.colspec(colwidth=30, colname='c'+str(idx))
            for idx, _ in enumerate(range(4)))

        thead = nodes.thead()
        tgroup += thead
        row_node = nodes.row()
        thead += row_node
        row_node.extend(nodes.entry(h, nodes.paragraph(text=h))
            for h in ["Version", "Code name", "Release date", "End of life"])

        tbody = nodes.tbody()
        tgroup += tbody

        timeline = []
        for code_name, info in six.iteritems(releases["releases"]):
            for release in info.get("releases", []):
                version = "Ceph {}".format(release["version"])
                released = release["released"]
                row_info = (version, code_name, released, "--")
                timeline.append(row_info)

        for release in releases["development"]["releases"]:
            version = "Ceph {}".format(release["version"])
            released = release["released"]
            row_info = (version, "--", released, "--")
            timeline.append(row_info)

        timeline = sorted(timeline, key=lambda t: t[2], reverse=True)

        rows = []
        for row_info in timeline:
            trow = nodes.row()

            entry = nodes.entry()
            para = nodes.paragraph(text=row_info[0])
            entry += para
            trow += entry

            entry = nodes.entry()
            para = nodes.paragraph(text=row_info[1])
            entry += para
            trow += entry

            entry = nodes.entry()
            para = nodes.paragraph(text=row_info[2])
            entry += para
            trow += entry

            entry = nodes.entry()
            para = nodes.paragraph(text=row_info[3])
            entry += para
            trow += entry

            rows.append(trow)

        tbody.extend(rows)

        #for row in range(10):
        #    for cell in range(3):
        #        entry = nodes.entry()
        #        para = nodes.paragraph(text="`Mimic`_")
        #        sphinx.util.nodes.nested_parse_with_titles(
        #            self.state, para, entry) 
        #        trow += entry
        #    rows.append(trow)
        #tbody.extend(rows)

        return [table]

def setup(app):
    app.add_directive('ceph_releases', CephReleases)
    app.add_directive('ceph_timeline', CephTimeline)
