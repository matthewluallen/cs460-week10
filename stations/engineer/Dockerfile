FROM python:3.12-slim
COPY lib/ /lib/
COPY engineer/engineer.py /app/engineer.py
# default: sit on the network and poll the controller; override the command to write.
CMD ["python3", "/app/engineer.py", "read"]
