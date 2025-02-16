import torch
from matplotlib import pyplot as plt
import cv2
from PIL import Image
import pathlib
temp = pathlib.PosixPath
pathlib.PosixPath = pathlib.WindowsPath
import paho.mqtt.client as mqtt
import json

# Add the rest of the code from the notebook here
def get_model():
    model = torch.hub.load('ultralytics/yolov5', 'custom', path='best.pt')  # 'best.pt' là file model sau khi train xong
    return model

def get_prediction(model, img):
    results = model(img)
    return results

def show_prediction(img, results):
    results.show()
    plt.imshow(img)
    plt.show()

def get_image(path):
    img = Image.open(path)
    return img

def webcam_detection(model):
    cap = cv2.VideoCapture(0)
    while True:
        ret, frame = cap.read()
        results = model(frame)
        result_image = results.render()[0]  # Kết quả render là một numpy array
        # Hiển thị ảnh với bounding box
        cv2.imshow('YOLOv5 Detection', result_image)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cap.release()
    cv2.destroyAllWindows()
    

def get_image_from_camera():
    cap = cv2.VideoCapture(0)
    ret, frame = cap.read()
    cap.release()
    return frame

def get_image_from_mqtt():
    def on_connect(client, userdata, flags, rc):
        print("Connected with result code "+str(rc))
        client.subscribe("image")

    def on_message(client, userdata, msg):
        print(msg.topic+" "+str(msg.payload))
        global image
        image = msg.payload

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect("localhost", 1883, 60)
    client.loop_forever()
    return image

def detect_and_publish(model,image_path, client, topic):
    # Đọc và dự đoán
    img = Image.open(image_path)
    results = model(img)
    
    # Chuyển đổi kết quả thành chuỗi JSON
    results_json = results.pandas().xyxy[0].to_json(orient='records')
    
    # Phân tích dữ liệu JSON và lấy tên lớp
    results_data = json.loads(results_json)
    if results_data:  # Kiểm tra nếu có dữ liệu
        label = results_data[0]['name']  # Lấy tên lớp từ kết quả đầu tiên
    else:
        label = 'No detection'

    # Gửi dữ liệu lên MQTT
    client.publish(topic, label)
    results.print()
    results.show()

def main():
    model = get_model()
    broker = 'broker.hivemq.com'
    port = 1883  # Chọn cổng nếu cần
    topic = 'waste/recognition'

    # For testing with real life garbage using webcam   
    # webcam_detection(model)

    # For running the system via MQTT image
    # img = get_image_from_mqtt()
    # results = get_prediction(model, img)
    # show_prediction(img, results)

    # For running the system via input image and publish the result 
    def on_connect(client, userdata, flags, rc):
        print(f'Connected with result code {rc}')
    # Callback khi gửi tin nhắn thành công
    def on_publish(client, userdata, mid):
        print(f'Message {mid} published.')
    # Tạo client MQTT
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_publish = on_publish
    # Kết nối đến broker
    client.connect(broker, port, 60)
    client.loop_start()

    img = input("Enter the image path: ")
    detect_and_publish(model, img, client, topic)

if __name__ == "__main__":
    main()
