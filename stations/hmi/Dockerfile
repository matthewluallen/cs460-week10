FROM python:3.12-slim
COPY lib/ /lib/
COPY hmi/hmi.py /app/hmi.py
EXPOSE 8800
CMD ["python3", "/app/hmi.py"]
