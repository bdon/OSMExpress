from collections import namedtuple
import sys
import xml.etree.ElementTree as ET
import xml.dom.minidom
import osmx

# generates an augmented diff for an OSC (OsmChange) file.
# see https://wiki.openstreetmap.org/wiki/Overpass_API/Augmented_Diffs
# this is intended to be run before the OSC file is applied to the osmx file.

if len(sys.argv) < 4:
    print("Usage: augmented_diff.py OSMX_FILE OSC_FILE OUTPUT")
    exit(1)

# 1st pass: populate the actions

# dictionary from osm_type/osm_id to action
# e.g. node/12345 > Node()
Action = namedtuple('Action',['type','element'])
actions = {}

osc = ET.parse(sys.argv[2]).getroot()
for block in osc:
    if block.tag == 'create':
        for e in block:
            # obj = create_element(e)
            action_key = e.tag + "/" + e.get("id")
            if action_key in actions:
                # check the version TODO make sure this actually works
                if obj.get("version") > actions[action_key].element.get("version"):
                    continue
            actions[action_key] = Action('create',e)

def sort_by_type(x):
    if x.element.tag == 'node':
        return 1
    elif x.element.tag == 'way':
        return 2
    return 3

action_list = [v for k,v in actions.items()]
sorted(action_list, key=sort_by_type)
sorted(action_list, key=lambda x:x.element.get('id'))

# 2nd pass: fill in references
env = osmx.Environment(sys.argv[1])
with osmx.Transaction(env) as txn:
    locations = osmx.Locations(txn)
    ways = osmx.Ways(txn)

    def get_lat_lon(ref):
        if 'node' + ref in actions:
            node = actions['node/' + ref]
            return (node.element.get('lon'),node.element.get('lat'))
        else:
            ll = locations.get(ref)
            return (str(ll[0]),str(ll[1]))

    def augment_nd(nd):
        ll = get_lat_lon(nd.get('ref'))
        nd.set('lon',ll[0])
        nd.set('lat',ll[1])

    def augment_member(mem):
        if mem.get('type') == 'way':
            ref = mem.get('ref')
            if 'way/' + ref in actions:
                way = actions['way/' + ref]
                for child in way.element:
                    if child.tag == 'nd':
                        ll = get_lat_lon(child.get('ref'))
                        nd = ET.SubElement(mem,'nd')
                        nd.set('lon',ll[0])
                        nd.set('lat',ll[1])
                        nd.set('ref',child.get('ref'))
            else:
                for node_id in ways.get(ref).nodes:
                    ll = get_lat_lon(str(node_id))
                    nd = ET.SubElement(mem,'nd')
                    nd.set('lon',ll[0])
                    nd.set('lat',ll[1])
                    nd.set('ref',str(node_id))
        elif mem.get('type') == 'node':
            ll = get_lat_lon(mem.get('ref'))
            mem.set('lon',ll[0])
            mem.set('lat',ll[1])

    for action in action_list:
        if action.element.tag == 'way':
            for child in action.element:
                if child.tag == 'nd':
                    augment_nd(child)
        if action.element.tag == 'relation':
            for child in action.element:
                if child.tag == 'member':
                    augment_member(child)


o = ET.Element('osm')
o.set("version","0.6")
o.set("generator","osmexpress python augmented_diff")
note = ET.SubElement(o,'note')
note.text = "The data included in this document is from www.openstreetmap.org. The data is made available under ODbL."

for action in action_list:
    a = ET.SubElement(o,'action')
    a.set('type',action.type)
    old = ET.SubElement(a,'old')
    new = ET.SubElement(a,'new')
    if action.type == 'create':
        new.append(action.element)
    # if action.type == 'delete':
    #     old.append(action.element)
    # if action.type == 'modify':
    #     pass

# pretty print helper
# http://effbot.org/zone/element-lib.htm#prettyprint
def indent(elem, level=0):
    i = "\n" + level*"  "
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = i + "  "
        if not elem.tail or not elem.tail.strip():
            elem.tail = i
        for elem in elem:
            indent(elem, level+1)
        if not elem.tail or not elem.tail.strip():
            elem.tail = i
    else:
        if level and (not elem.tail or not elem.tail.strip()):
            elem.tail = i   

indent(o)
ET.ElementTree(o).write(sys.argv[3])
