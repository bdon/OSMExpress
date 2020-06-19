from collections import namedtuple
from datetime import datetime
import copy
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
    for e in block:
        action_key = e.tag + "/" + e.get("id")
        if action_key in actions:
            # check the version TODO make sure this actually works
            if obj.get("version") > actions[action_key].element.get("version"):
                continue
        actions[action_key] = Action(block.tag,e)

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
    nodes = osmx.Nodes(txn)
    ways = osmx.Ways(txn)
    relations = osmx.Relations(txn)

    def not_in_db(elem):
        elem_id = int(elem.get('id'))
        if elem.tag == 'node':
            return not locations.get(elem_id)
        elif elem.tag == 'way':
            return not ways.get(elem_id)
        else:
            return not relations.get(elem_id)

    def get_lat_lon(ref, use_new):
        if use_new and ('node/' + ref in actions):
            node = actions['node/' + ref]
            return (node.element.get('lon'),node.element.get('lat'))
        else:
            ll = locations.get(ref)
            return (str(ll[1]),str(ll[0]))

    def set_old_metadata(elem):
        elem_id = int(elem.get('id'))
        if elem.tag == 'node':
            o = nodes.get(elem_id)
        elif elem.tag == 'way':
            o = ways.get(elem_id)
        else:
            o = relations.get(elem_id)
        if o:
            elem.set('version',str(o.metadata.version))
            elem.set('user',str(o.metadata.user))
            elem.set('uid',str(o.metadata.uid))
            # convert to ISO8601 timestamp
            timestamp = o.metadata.timestamp
            formatted = datetime.utcfromtimestamp(timestamp).isoformat()
            elem.set('timestamp',formatted + 'Z')
            elem.set('changeset',str(o.metadata.changeset))
        else:
            # tagless nodes
            version = locations.get(elem_id)[2]
            elem.set('version',str(version))
            elem.set('user','?')
            elem.set('uid','?')
            elem.set('timestamp','?')
            elem.set('changeset','?')

    # Create a list of actions

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
        elif action.type == 'delete':
            # get the old metadata
            modified = copy.deepcopy(action.element)
            set_old_metadata(action.element)
            old.append(action.element)

            modified.set('visible','false')
            for child in list(modified):
                modified.remove(child)
            # TODO the Geofabrik deleted elements seem to have the old metadata and old version numbers
            # check if this is true of planet replication files
            new.append(modified)
        else:
            obj_id = action.element.get('id')
            if not_in_db(action.element):
                # TODO verify this is correct
                print("Could not find {0} {1} in db, changing to create".format(action.element.tag,action.element.get('id')))
                new.append(action.element)
                a.set('type','create')
            else:
                prev_version = ET.SubElement(old,action.element.tag)
                prev_version.set('id',obj_id)
                set_old_metadata(prev_version)
                if action.element.tag == 'node':
                    ll = get_lat_lon(obj_id,False)
                    prev_version.set('lon',ll[0])
                    prev_version.set('lat',ll[1])
                elif action.element.tag == 'way':
                    way = ways.get(obj_id)
                    for n in way.nodes:
                        node = ET.SubElement(prev_version,'nd')
                        node.set('ref',str(n))
                    it = iter(way.tags)
                    for t in it:
                        tag = ET.SubElement(prev_version,'tag')
                        tag.set('k',t)
                        tag.set('v',next(it)) 
                else:
                    relation = relations.get(obj_id)
                    for m in relation.members:
                        member = ET.SubElement(prev_version,'member')
                        member.set('ref',str(m.ref))
                        member.set('role',m.role)
                        member.set('type',str(m.type))
                    it = iter(relation.tags)
                    for t in it:
                        tag = ET.SubElement(prev_version,'tag')
                        tag.set('k',t)
                        tag.set('v',next(it)) 
                new.append(action.element)

    # Augment the created "old" and "new" elements

    def augment_nd(nd,use_new):
        ll = get_lat_lon(nd.get('ref'),use_new)
        nd.set('lon',ll[0])
        nd.set('lat',ll[1])

    def augment_member(mem,use_new):
        if mem.get('type') == 'way':
            ref = mem.get('ref')
            if use_new and ('way/' + ref in actions):
                way = actions['way/' + ref]
                for child in way.element:
                    if child.tag == 'nd':
                        ll = get_lat_lon(child.get('ref'),use_new)
                        nd = ET.SubElement(mem,'nd')
                        nd.set('lon',ll[0])
                        nd.set('lat',ll[1])
                        nd.set('ref',child.get('ref'))
            else:
                for node_id in ways.get(ref).nodes:
                    ll = get_lat_lon(str(node_id),use_new)
                    nd = ET.SubElement(mem,'nd')
                    nd.set('lon',ll[0])
                    nd.set('lat',ll[1])
                    nd.set('ref',str(node_id))
        elif mem.get('type') == 'node':
            ll = get_lat_lon(mem.get('ref'),use_new)
            mem.set('lon',ll[0])
            mem.set('lat',ll[1])

    def augment(elem,use_new):
        if len(elem) == 0:
            return
        if elem[0].tag == 'way':
            for child in elem[0]:
                if child.tag == 'nd':
                    augment_nd(child,use_new)
        elif elem[0].tag == 'relation':
            for child in elem[0]:
                if child.tag == 'member':
                    augment_member(child,use_new)

    for elem in o:
        if elem.tag == 'action':
            old = augment(elem[0],False)
            new = augment(elem[1],True)


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
