#!/usr/bin/env python3
"""Local product maintenance. JSON stdin; bounded, allowlisted actions; no shell input."""
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
import uuid
import zipfile

SCHEMA=10
IGNORED={'backups','releases','staging','maintenance','cache','.cache'}
MAX_BYTES=32*1024**3

def emit(value):
    print(json.dumps(value,ensure_ascii=False),flush=True)
def digest(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def process_identity(pid):
    try:return (Path("/proc")/str(pid)/"stat").read_text().rsplit(")",1)[1].split()[19]
    except (OSError,IndexError):return None
def stamp():return dt.datetime.now(dt.timezone.utc).isoformat(timespec='seconds')
def inside(root,path):
    p=Path(path).resolve();p.relative_to(root.resolve());return p
def checkdb(path,schema=None):
    with sqlite3.connect('file:'+str(path)+'?mode=ro',uri=True) as db:
        if db.execute('PRAGMA quick_check').fetchone()[0]!='ok':raise ValueError('SQLite完整性校验失败')
        if db.execute('PRAGMA foreign_key_check').fetchone():raise ValueError('数据库关联校验失败')
        if schema is not None and db.execute('SELECT version FROM schema_meta').fetchone()[0]!=schema:raise ValueError('数据库版本不兼容')
def copydb(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True)
    with sqlite3.connect('file:'+str(src)+'?mode=ro',uri=True) as a,sqlite3.connect(dst) as b:a.backup(b)
    checkdb(dst,SCHEMA if src.name=='field_inspection.sqlite' else None)
def regular_files(root):
    for current,dirs,files in os.walk(root,followlinks=False):
        dirs[:]=[d for d in dirs if d not in IGNORED and not (Path(current)/d).is_symlink()]
        for name in files:
            p=Path(current)/name
            if p.is_symlink() or name.endswith(('-wal','-shm','-journal','.lock')):continue
            yield p

def backup(root,destination):
    dst=Path(destination).expanduser().resolve()
    if not dst.is_absolute() or dst.suffix.lower()!='.zip':raise ValueError('备份路径必须为ZIP文件')
    if dst.exists():raise ValueError('备份文件已存在，请更换名称')
    dst.parent.mkdir(parents=True,exist_ok=True)
    work=root/'maintenance'/('backup-'+uuid.uuid4().hex);work.mkdir(parents=True)
    partial=dst.with_name(dst.name+'.partial')
    manifest={'format':'fieldops-backup/1','schema':SCHEMA,'created_at':stamp(),'files':[],'unmanaged':[]}
    try:
        files=list(regular_files(root));total=0
        estimate=sum(p.stat().st_size for p in files)
        if shutil.disk_usage(root).free<estimate+200*1024**2:raise ValueError('剩余空间不足以创建安全备份')
        # SQLite backup obtains coherent DBs even in WAL mode. Referenced attachments checked below.
        for src in files:
            if src==dst or src==partial or dst in src.parents:continue
            rel=src.relative_to(root);target=work/rel;target.parent.mkdir(parents=True,exist_ok=True)
            if src.suffix in {'.sqlite','.db'}:copydb(src,target)
            else:
                before=src.stat();shutil.copy2(src,target);after=src.stat()
                if before.st_size!=after.st_size or before.st_mtime_ns!=after.st_mtime_ns:raise ValueError('文件正在写入，请停止采集后备份：'+str(rel))
            total+=target.stat().st_size
            if total>MAX_BYTES:raise ValueError('备份超过32GiB，请先归档')
        # Copy external photo evidence into package and remember original paths for restore remapping.
        main=work/'field_inspection.sqlite'
        if not main.exists():raise ValueError('业务数据库不存在')
        with sqlite3.connect(main) as db:
            refs=[]
            for table,col in [('attachments','path'),('product_evidence','path'),('inspection_records','image_path'),('inspection_check_items','image_path')]:
                refs += [row[0] for row in db.execute('SELECT '+col+' FROM '+table+' WHERE '+col+" IS NOT NULL AND "+col+"<>''")]
            for (body,) in db.execute('SELECT messages FROM chat_conversations'):
                for row in json.loads(body):
                    refs.extend(row[k] for k in ['imagePath','audioPath'] if row.get(k))
            for value in set(refs):
                source=Path(value)
                if not source.is_file():raise ValueError('引用附件缺失：'+value)
                try:source.resolve().relative_to(root)
                except ValueError:
                    rel=Path('external')/(digest(source)+source.suffix.lower());target=work/rel;target.parent.mkdir(exist_ok=True);shutil.copy2(source,target)
                    manifest['unmanaged'].append({'original':value,'file':rel.as_posix()})
        for src in regular_files(work):
            manifest['files'].append({'path':src.relative_to(work).as_posix(),'size':src.stat().st_size,'sha256':digest(src)})
        with zipfile.ZipFile(partial,'w',zipfile.ZIP_DEFLATED,compresslevel=1) as z:
            for f in manifest['files']:z.write(work/f['path'],f['path'])
            z.writestr('manifest.json',json.dumps(manifest,ensure_ascii=False))
        os.chmod(partial,0o600)
        verify_archive(partial,root/'maintenance'/('verify-'+uuid.uuid4().hex),extract=False)
        os.replace(partial,dst)
        return {'ok':True,'path':str(dst),'files':len(manifest['files']),'bytes':total,'sha256':digest(dst)}
    finally:
        shutil.rmtree(work,ignore_errors=True)
        if partial.exists():partial.unlink()

def upgrade_workflow_copy(path):
    """Upgrade only an extracted, verified v7 copy; never alter the backup archive."""
    migration=Path(__file__).resolve().parent.parent/'migrations/008_workflow.sql'
    with sqlite3.connect(path) as db:
        if db.execute('SELECT version FROM schema_meta').fetchone()[0]!=7:raise ValueError('旧备份数据库版本不匹配')
        queries=["SELECT r.id FROM inspection_records r LEFT JOIN assets a ON a.id=r.asset_id LEFT JOIN inspection_tasks t ON t.id=r.task_id WHERE a.id IS NULL OR (COALESCE(r.task_id,'')<>'' AND (t.id IS NULL OR (COALESCE(t.asset_id,'')<>'' AND t.asset_id<>r.asset_id))) LIMIT 1","SELECT d.id FROM defects d LEFT JOIN assets a ON a.id=d.asset_id LEFT JOIN inspection_records r ON r.id=d.record_id WHERE a.id IS NULL OR (COALESCE(d.record_id,'')<>'' AND (r.id IS NULL OR r.asset_id<>d.asset_id)) LIMIT 1"]
        for sql in queries:
            if db.execute(sql).fetchone():raise ValueError('旧备份存在不一致的业务关联，请先人工核对')
        db.execute('BEGIN IMMEDIATE')
        for sql in migration.read_text(encoding='utf-8').splitlines():
            if sql.strip():db.execute(sql)
    checkdb(path,8)

def upgrade_face_copy(path):
    migration=Path(__file__).resolve().parent.parent/'migrations/009_face_access.sql'
    checkdb(path,8)
    with sqlite3.connect(path) as db:
        db.execute('BEGIN IMMEDIATE')
        for sql in migration.read_text(encoding='utf-8').splitlines():
            if sql.strip():db.execute(sql)
    checkdb(path,9)

def upgrade_face_pose_copy(path):
    migration=Path(__file__).resolve().parent.parent/'migrations/010_face_pose_templates.sql'
    checkdb(path,9)
    with sqlite3.connect(path) as db:
        db.execute('BEGIN IMMEDIATE')
        for sql in migration.read_text(encoding='utf-8').splitlines():
            if sql.strip():db.execute(sql)
    checkdb(path,SCHEMA)

def verify_archive(path,target,extract=True):
    path=Path(path).resolve();target=Path(target).resolve()
    if not path.is_file():raise ValueError('备份文件不存在')
    with zipfile.ZipFile(path) as z:
        names=z.namelist()
        if len(names)!=len(set(names)) or len(names)>100000:raise ValueError('重复或过量文件')
        if 'manifest.json' not in names:raise ValueError('缺少备份清单')
        info=z.getinfo('manifest.json')
        if info.file_size>10*1024**2:raise ValueError('清单过大')
        m=json.loads(z.read('manifest.json'))
        if m.get('format')!='fieldops-backup/1' or m.get('schema') not in (7,8,9,SCHEMA):raise ValueError('备份格式或数据库版本不兼容')
        expected={f['path']:f for f in m['files']}
        if set(names)!=(set(expected)|{'manifest.json'}):raise ValueError('清单与文件不一致')
        total=0
        if extract:target.mkdir(parents=True,exist_ok=False)
        try:
            for info in z.infolist():
                name=info.filename;p=Path(name)
                if p.is_absolute() or '..' in p.parts or '\\' in name or ':' in name or info.is_dir() or (info.external_attr>>16)&0o170000==0o120000:raise ValueError('不安全的备份路径')
                if name=='manifest.json':continue
                if p.parts[0] in IGNORED:raise ValueError('备份含禁止恢复的目录')
                f=expected[name];total+=info.file_size
                if total>MAX_BYTES or info.file_size!=f['size']:raise ValueError('备份大小异常')
                h=hashlib.sha256();out=None
                if extract:
                    dest=inside(target,target/name);dest.parent.mkdir(parents=True,exist_ok=True);out=open(dest,'xb')
                with z.open(info) as src:
                    while True:
                        data=src.read(1024*1024)
                        if not data:break
                        h.update(data)
                        if out:out.write(data)
                if out:out.close()
                if h.hexdigest()!=f['sha256']:raise ValueError('附件校验失败：'+name)
            if extract:
                if m['schema'] in (7,8,9):
                    if m['schema']==7:
                        checkdb(target/'field_inspection.sqlite',7);upgrade_workflow_copy(target/'field_inspection.sqlite')
                    if m['schema'] < 9:
                        upgrade_face_copy(target/'field_inspection.sqlite')
                    upgrade_face_pose_copy(target/'field_inspection.sqlite')
                    for f in m['files']:
                        if f['path']=='field_inspection.sqlite':f.update(size=(target/f['path']).stat().st_size,sha256=digest(target/f['path']))
                    m['upgraded_from']=m['schema'];m['schema']=SCHEMA
                checkdb(target/'field_inspection.sqlite',SCHEMA)
                for f in m['files']:
                    if f['path'].endswith(('.sqlite','.db')):checkdb(target/f['path'])
                (target/'manifest.json').write_text(json.dumps(m),encoding='utf-8')
        except Exception:
            if extract:shutil.rmtree(target,ignore_errors=True)
            raise
    return m

def integrity(root):
    missing=[];changed=[];checked=0;proof={}
    with sqlite3.connect('file:'+str(root/'field_inspection.sqlite')+'?mode=ro',uri=True) as db:
        for table in ['attachments','product_evidence']:
            for row in db.execute('SELECT id,path,sha256 FROM '+table+" WHERE path<>''"):
                p=Path(row[1]);checked+=1
                if not p.is_file():missing.append({'id':row[0],'path':row[1]})
                else:
                    h=digest(p);stat=p.stat();proof[str(p)]={'sha256':h,'size':stat.st_size,'mtime_ms':stat.st_mtime_ns//1000000}
                    if row[2] and h!=row[2]:changed.append({'id':row[0],'path':row[1]})
    # Acoustic reports reference immutable snapshots, all charts, computed data and original WAVs.
    acoustic=root/'acoustic/evaluations.sqlite'
    if acoustic.exists():
        checkdb(acoustic)
        def verify_report_file(identity,path,expected):
            nonlocal checked
            p=Path(path);checked+=1
            if not p.is_file():missing.append({'id':identity,'path':str(p)});return
            h=digest(p);stat=p.stat();proof[str(p)]={'sha256':h,'size':stat.st_size,'mtime_ms':stat.st_mtime_ns//1000000}
            if not expected or h!=expected:changed.append({'id':identity,'path':str(p)})
        with sqlite3.connect('file:'+str(acoustic)+'?mode=ro',uri=True) as reports:
            for report_id,revision,body,expected in reports.execute('SELECT report_id,revision,snapshot,sha256 FROM versions'):
                identity=report_id+'/v'+str(revision);folder=inside(root,root/'acoustic/reports'/report_id/('v'+str(revision)))
                if hashlib.sha256(body.encode('utf-8')).hexdigest()!=expected:changed.append({'id':identity,'path':str(acoustic),'reason':'report snapshot database hash'})
                verify_report_file(identity,folder/'snapshot.json',expected)
                snapshot=json.loads(body)
                for item in snapshot.get('images',[])+snapshot.get('sources',[]):verify_report_file(identity,item['path'],item.get('sha256'))
                if snapshot.get('analysis_data_path'):verify_report_file(identity,snapshot['analysis_data_path'],snapshot.get('analysis_data_sha256'))
                manifest=folder/'manifest.json'
                if not manifest.is_file():missing.append({'id':identity,'path':str(manifest)})
                else:
                    m=json.loads(manifest.read_text());verify_report_file(identity,folder/'report.pdf',m.get('pdf_sha256'))
                    for page in m.get('pages',[]):verify_report_file(identity,inside(root,folder/page['file']),page.get('sha256'))
    return {'ok':True,'healthy':not missing and not changed,'checked':checked,'missing':missing,'changed':changed,'proof':proof}

def cleanup_candidates(root,days):
    # Conservatively retain all business/measurement evidence. Only known regenerable temporary files.
    cutoff=time.time()-days*86400;out=[]
    for folder in ['staging','cache']:
        base=root/folder
        if not base.exists():continue
        for p in base.rglob('*'):
            if p.is_file() and not p.is_symlink() and p.stat().st_mtime<cutoff:
                out.append({'path':str(inside(root,p)),'size':p.stat().st_size,'sha256':digest(p)})
    return out

def run(action,a):
    root=Path(a['data_root']).resolve()
    if not root.is_dir() or not (root/'field_inspection.sqlite').is_file():raise ValueError('无效数据目录')
    maint=root/'maintenance';maint.mkdir(exist_ok=True)
    if action=='ingest':
        src=Path(a['path']).resolve()
        if not src.is_file() or src.is_symlink() or src.stat().st_size>2*1024**3:raise ValueError('附件无效或超过2GiB')
        if src.suffix.lower() not in {'.png','.jpg','.jpeg','.webp','.bmp','.wav','.mp3','.flac','.ogg','.mp4','.csv','.json','.pdf','.txt'}:raise ValueError('附件格式不支持')
        before=src.stat();h=digest(src);dst=root/'evidence'/'objects'/(h+src.suffix.lower());dst.parent.mkdir(parents=True,exist_ok=True)
        if not dst.exists():
            temp=dst.with_suffix(dst.suffix+'.partial');shutil.copy2(src,temp)
            if digest(temp)!=h:temp.unlink();raise ValueError('附件复制时发生变化')
            os.replace(temp,dst)
        after=src.stat()
        if before.st_size!=after.st_size or before.st_mtime_ns!=after.st_mtime_ns:raise ValueError('附件正在写入，稍后重试')
        return {'ok':True,'path':str(dst),'sha256':h}
    if action=='report_package':
        dst=Path(a['path']).resolve()
        if dst.suffix.lower()!='.zip' or dst.exists():raise ValueError('请选择新的ZIP保存路径')
        with sqlite3.connect('file:'+str(root/'field_inspection.sqlite')+'?mode=ro',uri=True) as db:
            row=db.execute('SELECT snapshot,sha256 FROM report_versions WHERE id=?',(a['report_id'],)).fetchone()
        if not row or hashlib.sha256(row[0].encode()).hexdigest()!=row[1]:raise ValueError('报告校验失败')
        doc=json.loads(row[0]);manifest={'format':'fieldops-report-package/1','report_sha256':row[1],'files':[]}
        dst.parent.mkdir(parents=True,exist_ok=True);partial=dst.with_name(dst.name+'.partial')
        try:
            with zipfile.ZipFile(partial,'w',zipfile.ZIP_DEFLATED,compresslevel=1) as z:
                z.writestr('report.json',row[0])
                for i,e in enumerate(doc.get('evidence',[])):
                    if not e.get('path'):continue
                    p=Path(e['path'])
                    if not p.is_file():raise ValueError('原始证据缺失：'+e['path'])
                    h=digest(p)
                    if h!=e.get('sha256'):raise ValueError('原始证据校验失败')
                    name='evidence/'+str(i+1)+'-'+h+p.suffix.lower();z.write(p,name)
                    manifest['files'].append({'citation':e.get('citation'),'path':name,'sha256':h,'original_path':e['path']})
                z.writestr('manifest.json',json.dumps(manifest,ensure_ascii=False))
            with zipfile.ZipFile(partial) as verify:
                for f in manifest['files']:
                    h=hashlib.sha256()
                    with verify.open(f['path']) as src:
                        for b in iter(lambda:src.read(1024*1024),b''):h.update(b)
                    if h.hexdigest()!=f['sha256']:raise ValueError('打包期间证据发生变化')
            os.chmod(partial,0o600);os.replace(partial,dst)
        finally:
            if partial.exists():partial.unlink()
        return {'ok':True,'path':str(dst),'evidence_files':len(manifest['files']),'sha256':digest(dst)}
    if action=='backup':return backup(root,a['path'])
    if action=='verify_backup':
        m=verify_archive(a['path'],maint/'unused',False);return {'ok':True,'schema':m['schema'],'files':len(m['files']),'created_at':m['created_at']}
    if action=='integrity':
        checkdb(root/'field_inspection.sqlite',SCHEMA);return integrity(root)
    if action=='cleanup_preview':
        days=int(a.get('days',90))
        if days<7 or days>3650:raise ValueError('保留期限为7至3650天')
        items=cleanup_candidates(root,days);token=uuid.uuid4().hex
        (maint/(token+'.json')).write_text(json.dumps({'action':'cleanup','created':time.time(),'days':days,'items':items}),encoding='utf-8')
        with sqlite3.connect(root/'field_inspection.sqlite') as db:db.execute("UPDATE product_settings SET value=? WHERE key='retention_days'",(str(days),))
        return {'ok':True,'token':token,'files':len(items),'bytes':sum(x['size'] for x in items),'scope':'仅清理暂存和缓存，巡检档案、证据和原始录音保留'}
    if action=='cleanup':
        token=str(a.get('token',''))
        if len(token)!=32 or any(c not in '0123456789abcdef' for c in token):raise ValueError('预览令牌无效')
        plan=json.loads((maint/(token+'.json')).read_text())
        if plan.get('action')!='cleanup' or time.time()-plan['created']>600:raise ValueError('预览已过期，请重新预览')
        moved=[];trash=root/'maintenance'/'trash'/token;trash.mkdir(parents=True)
        for item in plan['items']:
            p=inside(root,item['path']);rel=p.relative_to(root)
            if rel.parts[0] not in {'staging','cache'}:raise ValueError('清理范围无效')
            if not p.exists() or digest(p)!=item['sha256']:continue
            dst=trash/rel;dst.parent.mkdir(parents=True,exist_ok=True);os.replace(p,dst);moved.append(item)
        (trash/'undo.json').write_text(json.dumps(moved),encoding='utf-8');(maint/(token+'.json')).unlink()
        return {'ok':True,'files':len(moved),'undo_token':token,'recoverable':True}
    if action=='cleanup_undo':
        token=str(a['token'])
        if len(token)!=32 or not all(c in '0123456789abcdef' for c in token):raise ValueError('恢复令牌无效')
        trash=maint/'trash'/token;items=json.loads((trash/'undo.json').read_text());n=0
        for item in items:
            dst=inside(root,item['path']);src=inside(trash,trash/dst.relative_to(root))
            if dst.exists():raise ValueError('原路径已存在，未覆盖：'+str(dst))
            if not src.is_file() or digest(src)!=item['sha256']:raise ValueError('回收文件校验失败')
        for item in items:
            dst=inside(root,item['path']);src=inside(trash,trash/dst.relative_to(root));dst.parent.mkdir(parents=True,exist_ok=True);os.replace(src,dst);n+=1
        shutil.rmtree(trash);return {'ok':True,'restored':n}
    if action=='restore_prepare':
        token=uuid.uuid4().hex;target=maint/('restore-'+token);m=verify_archive(a['path'],target)
        (maint/(token+'.json')).write_text(json.dumps({'action':'restore','created':time.time(),'target':str(target),'archive_hash':digest(a['path'])}),encoding='utf-8')
        return {'ok':True,'token':token,'files':len(m['files']),'created_at':m['created_at'],'requires_restart':True}
    if action in {'restore_apply','apply_release','rollback_release'}:
        token=str(a.get('token',''))
        if len(token)!=32 or not all(c in '0123456789abcdef' for c in token):raise ValueError('校验令牌无效')
        plan=json.loads((maint/(token+'.json')).read_text())
        if time.time()-plan['created']>1800:raise ValueError('校验结果过期')
        # Detached installer waits for this exact process to close its databases. No forced termination.
        if (action=='restore_apply' and plan['action']!='restore') or (action!='restore_apply' and plan['action']!='release'):raise ValueError('操作不匹配')
        request=maint/('apply-'+uuid.uuid4().hex+'.json');request.write_text(json.dumps(a|{'plan':plan,'process_start':process_identity(int(a['pid']))}),encoding='utf-8');os.chmod(request,0o600)
        log=open(maint/'installer.log','a')
        subprocess.Popen([sys.executable,__file__,'_install',str(request)],stdin=subprocess.DEVNULL,stdout=log,stderr=log,start_new_session=True,close_fds=True)
        return {'ok':True,'restart_required':True,'message':'已排队；关闭应用后安装并重新启动'}
    if action=='releases':
        directory=root/'releases';directory.mkdir(exist_ok=True)
        return {'ok':True,'rows':[json.loads(p.read_text())|{'token':p.parent.name} for p in directory.glob('*/release.json')]}
    if action=='prepare_rollback':
        identity=str(a.get('release_id',''))
        if len(identity)>80 or not all(c in '0123456789abcdef-previous' for c in identity):raise ValueError('无效历史版本')
        target=inside(root/'releases',root/'releases'/identity)
        m=json.loads((target/'release.json').read_text())
        if m.get('schema')!=SCHEMA or digest(target/'app')!=m.get('sha256'):raise ValueError('历史版本校验失败')
        token=uuid.uuid4().hex
        (maint/(token+'.json')).write_text(json.dumps({'action':'release','created':time.time(),'target':str(target)}),encoding='utf-8')
        return {'ok':True,'token':token,'release':m,'requires_restart':True}
    if action in {'snapshot_release','stage_release'}:
        binary=Path(a.get('path') if action=='stage_release' else a['binary']).resolve()
        if not binary.is_file() or binary.stat().st_size>256*1024**2:raise ValueError('程序文件无效')
        with open(binary,'rb') as f:header=f.read(20)
        if header[:4]!=b'\x7fELF' or int.from_bytes(header[18:20],'little')!=183:raise ValueError('仅接受RK3588 ARM64程序')
        if action=='stage_release':
            meta=Path(str(binary)+'.json')
            if not meta.exists():raise ValueError('升级程序旁必须有同名.json版本清单')
            m=json.loads(meta.read_text())
            if m.get('schema')!=SCHEMA or m.get('sha256')!=digest(binary) or not m.get('version'):raise ValueError('升级清单、哈希或数据库版本不匹配')
        else:m={'version':a.get('version','1.1.0'),'schema':SCHEMA,'sha256':digest(binary)}
        token=uuid.uuid4().hex;target=root/'releases'/token;target.mkdir(parents=True)
        shutil.copy2(binary,target/'app');os.chmod(target/'app',0o755);m['created_at']=stamp();(target/'release.json').write_text(json.dumps(m),encoding='utf-8')
        (maint/(token+'.json')).write_text(json.dumps({'action':'release','created':time.time(),'target':str(target),'binary':str(Path(a['binary']).resolve())}),encoding='utf-8')
        return {'ok':True,'token':token,'release':m}
    if action=='restart_ai':
        result=subprocess.run(['systemctl','--user','restart','board-ai.service'],capture_output=True,text=True,timeout=20)
        if result.returncode:result=subprocess.run(['sudo','-n','systemctl','restart','board-ai.service'],capture_output=True,text=True,timeout=20)
        if result.returncode:raise ValueError('服务重启失败，需检查本机服务权限')
        return {'ok':True,'service':'board-ai.service'}
    raise ValueError('不支持的操作')

def install(request):
    req=Path(request);a=json.loads(req.read_text());root=Path(a['data_root']).resolve();plan=a['plan'];pid=int(a['pid'])
    identity=a.get('process_start')
    for _ in range(1200):
        if not identity or process_identity(pid)!=identity:break
        time.sleep(.1)
    else:raise ValueError('应用尚未退出，取消安装')
    log={'action':plan['action'],'started':stamp(),'ok':False}
    try:
        if plan['action']=='restore':
            target=inside(root/'maintenance',plan['target']);m=json.loads((target/'manifest.json').read_text())
            for f in m['files']:
                if digest(inside(target,target/f['path']))!=f['sha256']:raise ValueError('暂存备份被改变')
            checkdb(target/'field_inspection.sqlite',SCHEMA)
            backup_dir=root/'backups';backup_dir.mkdir(exist_ok=True);backup(root,str(backup_dir/('before-restore-'+uuid.uuid4().hex+'.zip')))
            # Rebase external attachments into managed package files.
            mapping={u['original']:str(root/u['file']) for u in m.get('unmanaged',[])}
            with sqlite3.connect(target/'field_inspection.sqlite') as db:
                # immutable report snapshots retain original source paths; source files are retained by restore in-place.
                for old,new in mapping.items():
                    for table,col in [('attachments','path'),('product_evidence','path'),('inspection_records','image_path'),('inspection_check_items','image_path')]:db.execute('UPDATE '+table+' SET '+col+'=? WHERE '+col+'=?',(new,old))
            # Directory swap of managed files is journaled and recoverable.
            rollback=root/'maintenance'/('previous-'+uuid.uuid4().hex);rollback.mkdir()
            current=[p for p in root.iterdir() if p.name not in IGNORED]
            installed=[]
            try:
                for p in current:os.replace(p,rollback/p.name)
                for p in target.iterdir():
                    if p.name=='manifest.json':continue
                    os.replace(p,root/p.name);installed.append(p.name)
                checkdb(root/'field_inspection.sqlite',SCHEMA)
            except Exception:
                for name in installed:
                    p=root/name
                    if p.is_dir():shutil.rmtree(p)
                    elif p.exists():p.unlink()
                for p in rollback.iterdir():os.replace(p,root/p.name)
                raise
            log['rollback_path']=str(rollback)
        else:
            target=inside(root/'releases',plan['target']);m=json.loads((target/'release.json').read_text());binary=Path(a['binary']).resolve()
            if m['schema']!=SCHEMA or digest(target/'app')!=m['sha256']:raise ValueError('程序包校验失败')
            checkdb(root/'field_inspection.sqlite',SCHEMA)
            old=root/'releases'/('previous-'+uuid.uuid4().hex);old.mkdir(parents=True);shutil.copy2(binary,old/'app');(old/'release.json').write_text(json.dumps({'schema':SCHEMA,'version':'previous','sha256':digest(binary),'created_at':stamp()}))
            temp=binary.with_name(binary.name+'.new');shutil.copy2(target/'app',temp);os.chmod(temp,0o755);os.replace(temp,binary);log['previous_release']=str(old)
        log['ok']=True
    finally:
        log['finished']=stamp();(root/'maintenance'/'last-install.json').write_text(json.dumps(log,ensure_ascii=False),encoding='utf-8');req.unlink(missing_ok=True)
        if os.environ.get('FIELDOPS_MAINTENANCE_NO_RESTART')!='1':
            subprocess.Popen([sys.executable,'/home/ztl/FieldInspectionAssistant/deploy/open_field_inspection.py'],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)

if __name__=='__main__':
    try:
        if sys.argv[1]=='_install':install(sys.argv[2])
        else:
            raw=sys.stdin.buffer.read(2*1024**2)
            emit(run(sys.argv[1],json.loads(raw)))
    except Exception as e:
        emit({'ok':False,'error':str(e)});sys.exit(1)
