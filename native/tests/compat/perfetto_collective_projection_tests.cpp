#include "../../src/compat/perfetto_export_internal.h"
#include "traceloom/testing/test_util.h"
#include <sqlite3.h>
#include <algorithm>
#include <memory>
#include <string>
using namespace traceloom::compat::perfetto_internal;
using traceloom::testing::require;
void sql(sqlite3* db,const char* text) {require(sqlite3_exec(db,text,nullptr,nullptr,nullptr)==SQLITE_OK);}
std::vector<TimelineSlice> input() {
  TimelineSlice task;task.event=true;task.replay=true;task.event_id="task";task.name="AivKernel";
  task.start=20;task.end=25;task.stream=7;task.anchor_index=8;task.launch_id="launch";task.args="{}";
  auto op=task;op.replay=false;op.event_id="op";op.name="AllReduce";op.launch_id="";
  auto phase=task;phase.event=false;phase.name="attention";phase.phase_label="attention";
  phase.start=10;phase.end=18;phase.domain_id="compute";phase.position_id="attention-position";
  phase.position_start=0;phase.position_end=4;phase.stream=9;
  auto residual=phase;residual.phase_label="residual_norm";residual.start=30;residual.end=35;
  residual.position_start=4;residual.position_end=6;
  auto decoration=task;decoration.event=false;decoration.repeat_body=true;
  decoration.position_start=0;decoration.position_end=1;decoration.phase_label="";
  return {task,op,phase,residual,decoration};
}
int main() {
  {
    TimelineSlice aicpu; aicpu.event=true; aicpu.name="KERNEL_AICPU";
    TimelineSlice aiv=aicpu; aiv.name="AivKernel";
    TimelineSlice sqe=aicpu; sqe.name="FUTURE_COMMAND_SQE";
    TimelineSlice ordinary=aicpu; ordinary.name="MatMulV2_ND_ND_FP16_FP16_false_true_all_98513";
    TimelineSlice fused=aicpu; fused.name="QuantBatchMatmulAllReduce_506a984e26ea19e14052d4a5eac3f461_260";
    TimelineSlice structure=aicpu; structure.event=false; structure.name="AivKernel";
    std::vector<TimelineSlice> slices{ordinary,aicpu,fused,aiv,sqe,structure};
    apply_device_event_display_policy(slices, "ascend");
    require(slices.size()==3);
    require(slices[0].name=="MatMul");
    require(slices[1].name=="QuantBatchMatmulAllReduce");
    require(!slices[2].event && slices[2].name=="AivKernel");
    std::vector<TimelineSlice> cuda{aicpu, ordinary};
    apply_device_event_display_policy(cuda, "cuda");
    require(cuda.size()==2 && cuda[0].name=="KERNEL_AICPU" &&
            cuda[1].name==ordinary.name);
  }
  sqlite3* raw=nullptr;require(sqlite3_open(":memory:",&raw)==SQLITE_OK);
  std::unique_ptr<sqlite3,decltype(&sqlite3_close)> db(raw,sqlite3_close);
  sql(raw,R"(
CREATE TABLE traceloom_raw_table(source_id,source_path,source_table,embedded_table_name,source_rowid_column);
INSERT INTO traceloom_raw_table VALUES('source','/capture','TASK','TASK','rowid'),('source','/capture','COMMUNICATION_OP','COMMUNICATION_OP','rowid');
CREATE TABLE TASK(deviceId,startNs,endNs,connectionId);
CREATE TABLE COMMUNICATION_OP(deviceId,startNs,endNs,connectionId);
INSERT INTO TASK VALUES(0,20,25,7);
INSERT INTO COMMUNICATION_OP VALUES(0,20,25,7);
CREATE TABLE traceloom_event(db_idx,device_id,event_id,stream_id,start_ns,end_ns,symbol,source_table,source_key,raw_json);
INSERT INTO traceloom_event VALUES(0,0,'task',7,20,25,'hcom_allReduce_','TASK','1','{"source_path":"/capture"}'),(0,0,'op',7,20,25,'hcom_allReduce__1','COMMUNICATION_OP','1','{"source_path":"/capture"}');
)");
  auto good=input();project_collective_display(raw,good);
  require(good.size()==3); // hide only task and its one-member repeat decoration
  require(good[0].event_id=="op" && good[0].start==20 && good[0].end==25);
  require(good[0].args.find("display_folded_task_event_id")!=std::string::npos);
  require(good[1].end==25 && good[2].start==30);
  require(good[1].args.find("not_hpo_membership")!=std::string::npos);
  sql(raw,"SAVEPOINT gather");
  sql(raw,"UPDATE traceloom_event SET symbol='hcom_allGather_'");
  auto gather=input();gather[1].name="AllGather";
  project_collective_display(raw,gather);
  require(gather.size()==3 && gather[1].end==18); // fold, but no attention association
  sql(raw,"ROLLBACK TO gather");sql(raw,"RELEASE gather");
  for (int kind=0;kind<5;++kind) {
    auto v=input();
    if(kind==0)v[0].launch_id="other";
    if(kind==1)v[3].position_start=99;
    if(kind==2)v[0].name="DifferentKernel";
    if(kind==3)v.push_back(v[2]); // ambiguous phase ownership
    if(kind==4)v[3].start=24; // collective overlaps residual
    project_collective_display(raw,v);
    for(const auto& s:v)if(s.phase_label=="attention")require(s.end==18);
  }
  auto missing=input();missing.erase(missing.begin()+1);project_collective_display(raw,missing);
  require(missing.size()==4); // no visible counterpart: never erase real work
  for(auto mutation:{
      "UPDATE COMMUNICATION_OP SET connectionId=8",
      "UPDATE traceloom_event SET symbol='hcom_allGather_' WHERE event_id='op'",
      "UPDATE traceloom_event SET raw_json='{\"source_path\":\"/other\"}' WHERE event_id='op'",
      "UPDATE traceloom_event SET source_key='1suffix' WHERE event_id='op'",
      "UPDATE traceloom_event SET stream_id=8 WHERE event_id='op'",
      "UPDATE COMMUNICATION_OP SET endNs=26",
      "INSERT INTO traceloom_event SELECT * FROM traceloom_event WHERE event_id='op'"}) {
    sql(raw,"SAVEPOINT mutation");sql(raw,mutation);
    auto v=input();project_collective_display(raw,v);require(v.size()==5 && v[2].end==18);
    sql(raw,"ROLLBACK TO mutation");sql(raw,"RELEASE mutation");
  }
}
