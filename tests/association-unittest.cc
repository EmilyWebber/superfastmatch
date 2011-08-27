#include <kcprotodb.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <association.h>
#include <document.h>
#include <mock_registry.h>

using namespace testing;
using namespace superfastmatch;
using namespace kyotocabinet;

class AssociationTest :public ::testing::Test{
protected:
  PolyDB* documentDB_;
  PolyDB* metaDB_;
  PolyDB* associationDB_;
  DocumentManager* documentManager_;
  Logger* logger_;
  MockRegistry registry_;
  
  virtual void SetUp(){
    documentDB_ = new PolyDB();
    metaDB_ = new PolyDB();
    associationDB_ =  new PolyDB();
    documentManager_ = new DocumentManager(&registry_);
    documentDB_->open();
    metaDB_->open();
    associationDB_->open();
    logger_ = new Logger();
    EXPECT_CALL(registry_,getWindowSize())
      .WillRepeatedly(Return(15));
    EXPECT_CALL(registry_,getWhiteSpaceThreshold())
      .WillRepeatedly(Return(4));
    EXPECT_CALL(registry_,getWhiteSpaceHash(false))
      .WillRepeatedly(Return(0));
    EXPECT_CALL(registry_,getWhiteSpaceHash(true))
      .WillRepeatedly(Return(0));
    EXPECT_CALL(registry_,getDocumentDB())
      .WillRepeatedly(Return(documentDB_));
    EXPECT_CALL(registry_,getMetaDB())
      .WillRepeatedly(Return(metaDB_));
    EXPECT_CALL(registry_,getAssociationDB())
      .WillRepeatedly(Return(associationDB_));
    EXPECT_CALL(registry_,getDocumentManager())
      .WillRepeatedly(Return(documentManager_));
    EXPECT_CALL(registry_,getLogger())
      .WillRepeatedly(Return(logger_));
  }
  
  virtual void TearDown(){
    metaDB_->close();
    documentDB_->close();
    associationDB_->close();
    delete metaDB_;
    delete documentDB_;
    delete associationDB_;
    delete documentManager_;
    delete logger_;
  }
};

TEST_F(AssociationTest,ConstructorTest){
  DocumentPtr doc1 = registry_.getDocumentManager()->createPermanentDocument(1,1,"text=This+is+a+long+sentence+where+the+phrase+Always+Look+On+The+Bright+Side+Of+Life&title=Doc1");
  DocumentPtr doc2 = registry_.getDocumentManager()->createPermanentDocument(1,2,"text=Always+Look+On+The+Bright+Side+Of+Life+and+this+is+a+long+sentence&title=Doc2");
  EXPECT_STREQ("This is a long sentence where the phrase Always Look On The Bright Side Of Life",doc1->getText().c_str());
  EXPECT_STREQ("Always Look On The Bright Side Of Life and this is a long sentence",doc2->getText().c_str());
  Association* association = new Association(&registry_,doc1,doc2);
  EXPECT_EQ(2U,association->getResultCount());
  EXPECT_EQ(38U,association->getLength(0));
  EXPECT_STREQ("Always Look On The Bright Side Of Life",association->getToResult(0).c_str());
  EXPECT_STREQ(association->getToResult(0).c_str(),association->getFromResult(0).c_str());
  EXPECT_EQ(23U,association->getLength(1));
  EXPECT_STREQ("This is a long sentence",association->getFromResult(1).c_str());
  EXPECT_STRCASEEQ(association->getToResult(1).c_str(),association->getFromResult(1).c_str());
  EXPECT_EQ(61U,association->getTotalLength());
  EXPECT_EQ(2U,registry_.getDocumentDB()->count());
  EXPECT_EQ(0U,registry_.getAssociationDB()->count());
  EXPECT_EQ(true,association->save());
  EXPECT_EQ(2U,registry_.getDocumentDB()->count());
  EXPECT_EQ(2U,registry_.getAssociationDB()->count());
  Association* association2 = new Association(&registry_,doc1,doc2);
  EXPECT_EQ(2U,association2->getResultCount());  
  Association* association3 = new Association(&registry_,doc2,doc1);
  EXPECT_EQ(2U,association3->getResultCount());
  EXPECT_STRCASEEQ(association2->getFromResult(1).c_str(),association3->getFromResult(1).c_str());
  EXPECT_EQ(2U,registry_.getAssociationDB()->count());
}

TEST_F(AssociationTest, WhitespaceTest){
  DocumentPtr doc1 = registry_.getDocumentManager()->createTemporaryDocument("text=++++++++++++++++++++whitespace+test+with+a+long+sentence+*+*+*+*+*+*+*+*+*+*+*+*+");
  DocumentPtr doc2 = registry_.getDocumentManager()->createTemporaryDocument("text=++++++++++++++++++++whitespace+test+with+a+long+sentence+*+*+*+*+*+*+*+*+*+*+*+*+");
  Association* association = new Association(&registry_,doc1,doc2);
  EXPECT_STREQ("   whitespace test with a long sentence * *",association->getToResult(0).c_str());
}

TEST_F(AssociationTest, EndingTest){
  DocumentPtr doc1 = registry_.getDocumentManager()->createTemporaryDocument("text=This+is+Captain+Franklin");
  DocumentPtr doc2 = registry_.getDocumentManager()->createTemporaryDocument("text=This+is+Captain+Francis");
  Association* association = new Association(&registry_,doc1,doc2);
  EXPECT_STREQ("This is Captain Fran",association->getToResult(0).c_str());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}