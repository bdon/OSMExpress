@0xd3a7e843a9c03421;

struct Node {
  tags @0 :List(Text);
}

struct Way {
  nodes @0 :List(UInt64);
  tags @1 :List(Text);
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
}
