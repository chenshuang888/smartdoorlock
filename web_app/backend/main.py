import datetime
import hashlib
import os
import secrets
import traceback

from fastapi import FastAPI, Depends, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from jose import jwt
from pydantic import BaseModel
from sqlalchemy import create_engine, Column, Integer, String, DateTime, Text
from sqlalchemy.orm import Session, declarative_base

# =============================================================================
#  配置
# =============================================================================

DB_PATH = os.getenv("DB_PATH", "/app/data/doorlock.db")
SECRET_KEY = os.getenv("SECRET_KEY", "smart-door-lock-secret-key-change-me")
ALGORITHM = "HS256"
TOKEN_EXPIRE_DAYS = 30

# 确保数据库目录存在
os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)

engine = create_engine(f"sqlite:///{DB_PATH}", connect_args={"check_same_thread": False})
Base = declarative_base()

# =============================================================================
#  模型
# =============================================================================

class User(Base):
    __tablename__ = "users"
    id = Column(Integer, primary_key=True)
    username = Column(String(50), unique=True, nullable=False, index=True)
    password_hash = Column(String(128), nullable=False)
    created_at = Column(DateTime, default=datetime.datetime.utcnow)

class LockKey(Base):
    __tablename__ = "lock_keys"
    id = Column(Integer, primary_key=True)
    user_id = Column(Integer, unique=True, nullable=False)
    key_value = Column(String(64), nullable=False)
    updated_at = Column(DateTime, default=datetime.datetime.utcnow)

class Log(Base):
    __tablename__ = "logs"
    id = Column(Integer, primary_key=True)
    user_id = Column(Integer, nullable=False, index=True)
    action = Column(String(50), nullable=False)
    detail = Column(Text)
    created_at = Column(DateTime, default=datetime.datetime.utcnow)

Base.metadata.create_all(engine)

# =============================================================================
#  密码工具（用 hashlib 替代 bcrypt，避免编译依赖）
# =============================================================================

def hash_password(password: str) -> str:
    salt = secrets.token_hex(16)
    h = hashlib.pbkdf2_hmac("sha256", password.encode(), salt.encode(), 100000)
    return f"{salt}${h.hex()}"

def verify_password(password: str, stored: str) -> bool:
    parts = stored.split("$", 1)
    if len(parts) != 2:
        return False
    salt, hash_val = parts
    h = hashlib.pbkdf2_hmac("sha256", password.encode(), salt.encode(), 100000)
    return h.hex() == hash_val

# =============================================================================
#  Auth
# =============================================================================

security = HTTPBearer(auto_error=False)

def create_token(user_id: int) -> str:
    expire = datetime.datetime.utcnow() + datetime.timedelta(days=TOKEN_EXPIRE_DAYS)
    return jwt.encode({"sub": str(user_id), "exp": expire}, SECRET_KEY, algorithm=ALGORITHM)

def get_db():
    db = Session(engine)
    try:
        yield db
    finally:
        db.close()

def require_user(credentials: HTTPAuthorizationCredentials | None = Depends(security),
                 db: Session = Depends(get_db)) -> int:
    if not credentials:
        raise HTTPException(status_code=401, detail="请先登录")
    try:
        payload = jwt.decode(credentials.credentials, SECRET_KEY, algorithms=[ALGORITHM])
        uid = int(payload["sub"])
    except Exception:
        raise HTTPException(status_code=401, detail="登录已过期，请重新登录")
    user = db.get(User, uid)
    if not user:
        raise HTTPException(status_code=401, detail="用户不存在")
    return uid

# =============================================================================
#  Pydantic
# =============================================================================

class RegisterReq(BaseModel):
    username: str
    password: str

class LoginReq(BaseModel):
    username: str
    password: str

class KeyReq(BaseModel):
    key: str

class LogReq(BaseModel):
    action: str
    detail: str = ""

class LogResp(BaseModel):
    id: int
    action: str
    detail: str
    time: str

# =============================================================================
#  FastAPI
# =============================================================================

app = FastAPI(title="智能门锁", docs_url="/api/docs")
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])

# 全局异常处理：确保服务器错误返回 JSON
@app.exception_handler(Exception)
async def global_exception(request: Request, exc: Exception):
    traceback.print_exc()
    return JSONResponse(status_code=500, content={"detail": f"服务器内部错误: {str(exc)}"})

# ── 注册 ────────────────────────────────────────────────────────────────────

@app.post("/api/register")
def register(body: RegisterReq, db: Session = Depends(get_db)):
    if len(body.username) < 2 or len(body.username) > 20:
        raise HTTPException(400, "用户名需 2-20 个字符")
    if len(body.password) < 6:
        raise HTTPException(400, "密码至少 6 位")
    if db.query(User).filter(User.username == body.username).first():
        raise HTTPException(409, "用户名已存在")
    user = User(username=body.username,
                password_hash=hash_password(body.password))
    db.add(user)
    db.commit()
    return {"token": create_token(user.id), "username": user.username}

# ── 登录 ────────────────────────────────────────────────────────────────────

@app.post("/api/login")
def login(body: LoginReq, db: Session = Depends(get_db)):
    user = db.query(User).filter(User.username == body.username).first()
    if not user or not verify_password(body.password, user.password_hash):
        raise HTTPException(401, "用户名或密码错误")
    return {"token": create_token(user.id), "username": user.username}

# ── 当前用户 ────────────────────────────────────────────────────────────────

@app.get("/api/me")
def me(uid: int = Depends(require_user), db: Session = Depends(get_db)):
    user = db.get(User, uid)
    return {"username": user.username, "created_at": user.created_at.isoformat()}

# ── 密钥管理 ────────────────────────────────────────────────────────────────

@app.get("/api/key")
def get_key(uid: int = Depends(require_user), db: Session = Depends(get_db)):
    k = db.query(LockKey).filter(LockKey.user_id == uid).first()
    return {"key": k.key_value if k else ""}

@app.post("/api/key")
def save_key(body: KeyReq, uid: int = Depends(require_user), db: Session = Depends(get_db)):
    k = db.query(LockKey).filter(LockKey.user_id == uid).first()
    if k:
        k.key_value = body.key
        k.updated_at = datetime.datetime.utcnow()
    else:
        db.add(LockKey(user_id=uid, key_value=body.key))
    db.commit()
    return {"ok": True}

@app.delete("/api/key")
def delete_key(uid: int = Depends(require_user), db: Session = Depends(get_db)):
    db.query(LockKey).filter(LockKey.user_id == uid).delete()
    db.commit()
    return {"ok": True}

# ── 操作日志 ────────────────────────────────────────────────────────────────

@app.get("/api/logs")
def get_logs(uid: int = Depends(require_user), db: Session = Depends(get_db)):
    rows = db.query(Log).filter(Log.user_id == uid)\
              .order_by(Log.id.desc()).limit(50).all()
    return [LogResp(id=r.id, action=r.action,
                    detail=r.detail or "",
                    time=r.created_at.strftime("%m-%d %H:%M")) for r in rows]

@app.post("/api/logs")
def add_log(body: LogReq, uid: int = Depends(require_user), db: Session = Depends(get_db)):
    db.add(Log(user_id=uid, action=body.action, detail=body.detail))
    db.commit()
    return {"ok": True}
