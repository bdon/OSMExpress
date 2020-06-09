import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
import osmx

if len(sys.argv) <= 1:
    print("Usage: web_server.py OSMX_FILE")

env  = osmx.Environment(sys.argv[1])

# simple implementation of OSM GeoJSON API using osmx + Python standard library.
# not production ready!

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parts = self.path.split("/")
        if len(parts) < 3:
            self.send_response(400)
            self.wfile.write("bad request".encode('utf-8'))
            return

        self.send_response(200)
        self.send_header('Content-type','application/json')
        self.end_headers()

        osm_id = parts[2]
        resp = {'type':'Feature','properties':{}}
        with osmx.Transaction(env) as txn:
            locations = osmx.Locations(txn)
            nodes = osmx.Nodes(txn)
            if parts[1] == "node":
                node = nodes.get(osm_id)
                if node:
                    for k,v in osmx.tag_dict(node.tags).items():
                        resp['properties'][k] = v

                loc = locations.get(osm_id)
                resp['geometry'] = {'type':'Point','coordinates':loc}
            elif parts[1] == "way":
                ways = osmx.Ways(txn)
                way = ways.get(osm_id)
                for k,v in osmx.tag_dict(way.tags).items():
                    resp['properties'][k] = v

                coords = [locations.get(node_id) for node_id in way.nodes]
                resp['geometry'] = {'type':'LineString','coordinates':coords}
            elif parts[1] == "relation":
                ways = osmx.Ways(txn)
                relations = osmx.Relations(txn)
                relation = relations.get(osm_id)
                for k,v in osmx.tag_dict(relation.tags).items():
                    resp['properties'][k] = v

                geometries = []
                def add_relation_geoms(r):
                    for member in r.members:
                        if member.type == 'node':
                            geometries.append({'type':'Point','coordinates':locations.get(member.ref)})
                        if member.type == 'way':
                            way = ways.get(member.ref)
                            coords = [locations.get(node_id) for node_id in way.nodes]
                            geometries.append({'type':'LineString','coordinates':coords})
                        if member.type == 'relation':
                            add_relation_geoms(relations.get(member.ref))

                add_relation_geoms(relation)
                resp['geometry'] = {'type':'GeometryCollection','geometries':geometries}

        self.wfile.write(json.dumps(resp).encode('utf-8'))

print('Server listening on port 8000...')
httpd = HTTPServer(('', 8000), Handler)
httpd.serve_forever()
