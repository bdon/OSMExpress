@0xd3a7e843a9c03421;

struct Metadata {
  version @0 :UInt32;
  timestamp @1 :UInt64;
  changeset @2 :UInt32;
  uid @3 :UInt32;
  user @4 :Text;
}

struct Node {
  tags @0 :List(Text);
  metadata @1 :Metadata;
}

struct Way {
  nodes @0 :List(UInt64);
  tags @1 :List(Text);
  metadata @2 :Metadata;
}

struct RelationMember {
  ref @0 :UInt64;
  type @1 :Type;
  role @2 :Text;

  enum Type {
    node @0;
    way @1;
    relation @2;
  }
}

struct Relation {
  tags @0 :List(Text);
  members @1 :List(RelationMember);
  metadata @2 :Metadata;
}
