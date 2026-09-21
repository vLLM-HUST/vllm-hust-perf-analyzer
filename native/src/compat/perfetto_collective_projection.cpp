#include "perfetto_export_internal.h"
#include <sqlite3.h>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>

namespace traceloom::compat::perfetto_internal {
namespace {
using Statement=std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)>;
Statement query(sqlite3* db,const std::string& sql) {
  sqlite3_stmt* stmt=nullptr;
  if(sqlite3_prepare_v2(db,sql.c_str(),-1,&stmt,nullptr)!=SQLITE_OK)
    throw std::runtime_error("collective display query: "+std::string(sqlite3_errmsg(db)));
  return Statement(stmt,sqlite3_finalize);
}
bool next(sqlite3_stmt* s) {
  auto rc=sqlite3_step(s);
  if(rc!=SQLITE_ROW && rc!=SQLITE_DONE) throw std::runtime_error("collective display read failed");
  return rc==SQLITE_ROW;
}
std::string text(sqlite3_stmt* s,int c) {
  auto p=sqlite3_column_text(s,c);return p?reinterpret_cast<const char*>(p):"";
}
std::string identifier(const std::string& value) {
  std::string out="\"";for(char c:value) {out+=c;if(c=='\"')out+=c;}return out+'\"';
}
std::string collective_kind(const std::string& name) {
  if(name=="AllReduce" || name=="AIV_AllReduce" || name.rfind("hcom_allReduce_",0)==0)return "AllReduce";
  if(name=="AllGather" || name.rfind("hcom_allGather_",0)==0)return "AllGather";
  return "";
}
void annotate(sqlite3* db,TimelineSlice& slice,const std::string& key,const std::string& value) {
  auto q=query(db,"SELECT json_set(?, '$.' || ?, ?)");
  sqlite3_bind_text(q.get(),1,slice.args.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(q.get(),2,key.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(q.get(),3,value.c_str(),-1,SQLITE_TRANSIENT);
  if(next(q.get())) slice.args=text(q.get(),0);
}
using Event=std::tuple<int,int,std::string>;
using Evidence=std::tuple<std::string,std::string,int,int,std::int64_t,std::int64_t,std::int64_t,std::int64_t>;
struct Pair {std::vector<Event> tasks,ops;};
// Provider identity, not name/overlap alone: one TASK and one COMMUNICATION_OP
// in the SAME embedded source, connection, device, stream and exact interval.
std::map<Event,Event> duplicate_tasks(sqlite3* db) {
  auto exists=query(db,"SELECT COUNT(*) FROM sqlite_master WHERE name IN ('traceloom_raw_table','traceloom_event')");
  if(!next(exists.get()) || sqlite3_column_int(exists.get(),0)!=2) return {};
  auto sources=query(db,"SELECT source_id,source_path,source_table,embedded_table_name,source_rowid_column FROM traceloom_raw_table WHERE source_table IN ('TASK','COMMUNICATION_OP')");
  std::map<Evidence,Pair> groups;
  while(next(sources.get())) {
    const auto table=text(sources.get(),3),rowid=text(sources.get(),4);
    if(rowid.empty())continue;
    std::set<std::string> columns;
    auto info=query(db,"PRAGMA table_info("+identifier(table)+")");
    while(next(info.get()))columns.insert(text(info.get(),1));
    if(!columns.count("connectionId") || !columns.count("deviceId") ||
       !columns.count("startNs") || !columns.count("endNs"))continue;
    auto q=query(db,"SELECT e.db_idx,e.device_id,e.event_id,e.stream_id,e.start_ns,e.end_ns,r.connectionId,e.symbol FROM traceloom_event e JOIN "+identifier(table)+" r ON r."+identifier(rowid)+"=CAST(e.source_key AS INTEGER) WHERE e.source_key=CAST(r."+identifier(rowid)+" AS TEXT) AND e.source_table=? AND json_extract(e.raw_json,'$.source_path')=? AND r.deviceId=e.device_id AND r.startNs=e.start_ns AND r.endNs=e.end_ns AND r.connectionId>0 AND e.stream_id IS NOT NULL");
    const auto kind=text(sources.get(),2),path=text(sources.get(),1);
    sqlite3_bind_text(q.get(),1,kind.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(),2,path.c_str(),-1,SQLITE_TRANSIENT);
    while(next(q.get())) {
      const auto collective=collective_kind(text(q.get(),7));
      if(collective.empty())continue;
      const int dbi=sqlite3_column_int(q.get(),0),device=sqlite3_column_int(q.get(),1);
      auto& pair=groups[{text(sources.get(),0),collective,dbi,device,sqlite3_column_int64(q.get(),3),
          sqlite3_column_int64(q.get(),4),sqlite3_column_int64(q.get(),5),sqlite3_column_int64(q.get(),6)}];
      (kind=="TASK"?pair.tasks:pair.ops).push_back({dbi,device,text(q.get(),2)});
    }
  }
  std::map<Event,Event> out;
  for(const auto& [key,pair]:groups)
    if(pair.tasks.size()==1 && pair.ops.size()==1)out.emplace(pair.tasks[0],pair.ops[0]);
  return out;
}
}
void project_collective_display(sqlite3* db,std::vector<TimelineSlice>& slices) {
  const auto duplicates=duplicate_tasks(db);
  using DisplayEvent=std::tuple<int,int,std::string,std::string>;
  std::map<DisplayEvent,std::vector<std::size_t>> events;
  for(std::size_t i=0;i<slices.size();++i) {
    const auto& s=slices[i];
    if(s.event)events[{s.db,s.device,s.view,s.event_id}].push_back(i);
  }
  std::set<std::size_t> hidden;
  std::vector<std::size_t> collectives;
  using Decoration=std::tuple<int,int,std::string,std::int64_t,std::int64_t,std::int64_t,std::int64_t>;
  std::set<Decoration> decorations;
  for(std::size_t i=0;i<slices.size();++i) {
    const auto& task=slices[i];
    if(!task.event || task.name!="AivKernel")continue;
    const auto pair=duplicates.find({task.db,task.device,task.event_id});
    if(pair==duplicates.end())continue;
    const auto found=events.find({task.db,task.device,task.view,std::get<2>(pair->second)});
    if(found==events.end() || found->second.size()!=1 ||
       events[{task.db,task.device,task.view,task.event_id}].size()!=1)continue;
    auto& op=slices[found->second[0]];
    if(op.start!=task.start || op.end!=task.end || op.stream!=task.stream)continue;
    annotate(db,op,"display_folded_task_event_id",task.event_id);
    annotate(db,op,"display_fold_basis","same_source_connection_device_stream_exact_interval");
    // Preserve the provider event as the visible observation, and retain the
    // exact graph task's coordinates for display-only phase association.
    op.launch_id=task.launch_id;
    annotate(db,op,"display_graph_launch_id",task.launch_id);
    hidden.insert(i);
    if(collective_kind(op.name)=="AllReduce")collectives.push_back(found->second[0]);
    decorations.insert({task.db,task.device,task.view,task.anchor_index,task.stream,task.start,task.end});
  }
  for(std::size_t i=0;i<slices.size();++i) {
    auto& s=slices[i];
    if(s.replay && s.repeat_body && s.position_end-s.position_start==1 &&
       decorations.count({s.db,s.device,s.view,s.anchor_index,s.stream,s.start,s.end}))hidden.insert(i);
  }
  // A USER-SELECTED DISPLAY convention, not cross-stream HPO membership or
  // dependency inference. Require a unique collective in the exact launch's
  // gap before the adjacent residual phase; ambiguous candidates stay separate.
  using Boundary=std::tuple<int,int,std::string,std::string,std::string,int>;
  std::map<Boundary,std::vector<std::size_t>> residuals;
  for(std::size_t i=0;i<slices.size();++i) {
    const auto& b=slices[i];
    if(b.phase_label=="residual_norm")
      residuals[{b.db,b.device,b.view,b.launch_id,b.domain_id,b.position_start}].push_back(i);
  }
  std::map<std::size_t,std::vector<std::size_t>> claims;
  for(std::size_t i=0;i<slices.size();++i) {
    const auto& phase=slices[i];
    if(phase.phase_label!="attention" || phase.launch_id.empty())continue;
    const auto found=residuals.find({phase.db,phase.device,phase.view,phase.launch_id,phase.domain_id,phase.position_end});
    if(found==residuals.end() || found->second.size()!=1)continue;
    const auto boundary_start=slices[found->second[0]].start;
    if(boundary_start<phase.end)continue;
    std::vector<std::size_t> candidates;
    for(auto j:collectives) {
      const auto& op=slices[j];
      if(op.db==phase.db && op.device==phase.device && op.view==phase.view &&
         op.launch_id==phase.launch_id && op.start>=phase.end && op.end<=boundary_start)
        candidates.push_back(j);
    }
    if(candidates.size()==1)claims[candidates[0]].push_back(i);
  }
  for(const auto& [op_index,phases]:claims) {
    if(phases.size()!=1)continue;
    auto& phase=slices[phases[0]];auto& op=slices[op_index];
    annotate(db,phase,"display_compute_end_ns",std::to_string(phase.end));
    annotate(db,phase,"display_collective_event_id",op.event_id);
    annotate(db,phase,"display_phase_semantics","compute_plus_unique_same_launch_gap_allreduce; not_hpo_membership");
    annotate(db,op,"display_phase_position_id",phase.position_id);
    annotate(db,op,"display_phase_launch_id",phase.launch_id);
    phase.end=op.end;
  }
  std::size_t index=0;
  slices.erase(std::remove_if(slices.begin(),slices.end(),[&](const auto&) {return hidden.count(index++)!=0;}),slices.end());
}
}  // namespace traceloom::compat::perfetto_internal
